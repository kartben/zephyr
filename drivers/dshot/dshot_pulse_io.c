/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_dshot_pulse_io

#include <zephyr/device.h>
#include <zephyr/drivers/dshot.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "dshot_command.h"
#include "dshot_pulse_io_wave.h"

LOG_MODULE_REGISTER(dshot_pulse_io, CONFIG_DSHOT_LOG_LEVEL);

/* A frame lasts at most 18 bit periods of 6.667 us */
#define DSHOT_PIO_TX_TIMEOUT K_MSEC(10)

#define DSHOT_PIO_FRAME_STOP dshot_frame_encode(DSHOT_THROTTLE_STOP, false)

struct dshot_pio_channel_config {
	const struct device *pio;
	uint8_t index;
};

struct dshot_pio_config {
	const struct dshot_pio_channel_config *channels;
	uint8_t num_channels;
	enum dshot_speed speed;
};

struct dshot_pio_channel {
	struct pulse_io_channel *handle;
	struct dshot_pio_timing timing;
	/* Frame staged by the application, guarded by frame_lock */
	uint16_t staged;
	union {
		struct pulse_cell cells[DSHOT_PIO_MAX_CELLS];
		struct pulse_symbol symbols[DSHOT_PIO_MAX_SYMBOLS];
	} wave;
};

struct dshot_pio_data {
	/* Serializes transmissions and configuration changes */
	struct k_mutex lock;
	/* Guards the staged frames and command_active */
	struct k_spinlock frame_lock;
	bool command_active;
	bool configured;
	enum dshot_speed speed;
	struct dshot_pio_channel *channels;
	/* Frames being transmitted, guarded by lock */
	uint16_t *tx_frames;
};

static bool dshot_pio_all_stopped(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;

	for (uint8_t i = 0; i < config->num_channels; i++) {
		if (dshot_frame_value(data->channels[i].staged) != DSHOT_THROTTLE_STOP) {
			return false;
		}
	}

	return true;
}

static int dshot_pio_channel_apply(const struct dshot_pio_channel_config *cc,
				   struct dshot_pio_channel *ch, uint32_t bitrate)
{
	struct pulse_io_config pcfg = {
		.dir = PULSE_IO_DIR_TX,
		.idle_high = false,
	};
	struct dshot_pio_request req;
	struct pulse_io_caps caps;
	uint32_t resolution_hz;
	int ret;

	ret = pulse_io_get_capabilities(cc->pio, &caps);
	if (ret != 0) {
		return ret;
	}

	ret = dshot_pio_select(&caps, cc->index, bitrate, &req);
	if (ret != 0) {
		LOG_ERR("%s channel %u cannot generate %u bit/s", cc->pio->name, cc->index,
			bitrate);
		return ret;
	}

	pcfg.mode = req.mode;
	pcfg.resolution_hz = req.resolution_hz;
	if (req.mode == PULSE_IO_MODE_CELL) {
		pcfg.cell_period_ticks = DIV_ROUND_CLOSEST(req.resolution_hz, bitrate);
	}

	ret = pulse_io_channel_configure(cc->pio, ch->handle, &pcfg);
	if (ret != 0) {
		return ret;
	}

	ret = pulse_io_channel_get_resolution(cc->pio, ch->handle, &resolution_hz);
	if (ret == -ENOSYS) {
		LOG_WRN("%s cannot report its tick rate, assuming %u Hz", cc->pio->name,
			req.resolution_hz);
		resolution_hz = req.resolution_hz;
	} else if (ret != 0) {
		return ret;
	}

	ret = dshot_pio_timing_init(&ch->timing, req.mode, bitrate, resolution_hz, req.max_ticks);
	if (ret != 0) {
		LOG_ERR("%s channel %u: %u Hz tick cannot generate %u bit/s", cc->pio->name,
			cc->index, resolution_hz, bitrate);
		return ret;
	}

	/* The rounded tick rate can move the cell period by a tick or more */
	if ((req.mode == PULSE_IO_MODE_CELL) && (ch->timing.bit_ticks != pcfg.cell_period_ticks)) {
		pcfg.cell_period_ticks = ch->timing.bit_ticks;
		ret = pulse_io_channel_configure(cc->pio, ch->handle, &pcfg);
		if (ret != 0) {
			return ret;
		}
	}

	LOG_DBG("%s channel %u: %s mode, %u Hz tick, %u ticks per bit (%d ppm), high %u/%u",
		cc->pio->name, cc->index, (req.mode == PULSE_IO_MODE_CELL) ? "cell" : "symbol",
		resolution_hz, ch->timing.bit_ticks, ch->timing.rate_error_ppm,
		ch->timing.t0h_ticks, ch->timing.t1h_ticks);

	return 0;
}

static int dshot_pio_apply(const struct device *dev, enum dshot_speed speed)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	uint32_t bitrate = (uint32_t)speed * 1000U;
	int ret;

	data->configured = false;
	for (uint8_t i = 0; i < config->num_channels; i++) {
		ret = dshot_pio_channel_apply(&config->channels[i], &data->channels[i], bitrate);
		if (ret != 0) {
			return ret;
		}
	}
	data->speed = speed;
	data->configured = true;

	return 0;
}

static int dshot_pio_transmit(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	int first_err = 0;

	if (!data->configured) {
		return -EIO;
	}

	for (uint8_t i = 0; i < config->num_channels; i++) {
		const struct dshot_pio_channel_config *cc = &config->channels[i];
		struct dshot_pio_channel *ch = &data->channels[i];
		struct pulse_io_tx_req req = {0};
		size_t count = 0;
		int ret;

		if (ch->timing.mode == PULSE_IO_MODE_CELL) {
			ret = dshot_pio_encode_cells(&ch->timing, data->tx_frames[i],
						     ch->wave.cells, ARRAY_SIZE(ch->wave.cells),
						     &count);
			req.cells = ch->wave.cells;
		} else {
			ret = dshot_pio_encode_symbols(&ch->timing, data->tx_frames[i],
						       ch->wave.symbols,
						       ARRAY_SIZE(ch->wave.symbols), &count);
			req.symbols = ch->wave.symbols;
		}
		req.count = count;

		if (ret == 0) {
			ret = pulse_io_transmit_sync(cc->pio, ch->handle, &req,
						     DSHOT_PIO_TX_TIMEOUT);
		}
		if ((ret != 0) && (first_err == 0)) {
			first_err = ret;
		}
	}

	return first_err;
}

static void dshot_pio_snapshot(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->frame_lock);

	for (uint8_t i = 0; i < config->num_channels; i++) {
		data->tx_frames[i] = data->channels[i].staged;
	}

	k_spin_unlock(&data->frame_lock, key);
}

static int dshot_pio_configure(const struct device *dev, const struct dshot_config *cfg)
{
	struct dshot_pio_data *data = dev->data;
	enum dshot_speed previous = data->speed;
	k_spinlock_key_t key;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	key = k_spin_lock(&data->frame_lock);
	ret = dshot_pio_all_stopped(dev) ? 0 : -EBUSY;
	k_spin_unlock(&data->frame_lock, key);

	if ((ret == 0) && (!data->configured || (cfg->speed != previous))) {
		ret = dshot_pio_apply(dev, cfg->speed);
		if ((ret != 0) && (dshot_pio_apply(dev, previous) != 0)) {
			LOG_ERR("%s: cannot restore DShot%u", dev->name, previous);
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int dshot_pio_set_frame(const struct device *dev, uint8_t channel, uint16_t frame)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	k_spinlock_key_t key;
	int ret = 0;

	if (channel >= config->num_channels) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->frame_lock);
	if (data->command_active && (dshot_frame_value(frame) != DSHOT_THROTTLE_STOP)) {
		ret = -EBUSY;
	} else {
		data->channels[channel].staged = frame;
	}
	k_spin_unlock(&data->frame_lock, key);

	return ret;
}

static int dshot_pio_send(const struct device *dev)
{
	struct dshot_pio_data *data = dev->data;
	int ret;

	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	dshot_pio_snapshot(dev);
	ret = dshot_pio_transmit(dev);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int dshot_pio_stop(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	k_spinlock_key_t key;

	key = k_spin_lock(&data->frame_lock);
	for (uint8_t i = 0; i < config->num_channels; i++) {
		data->channels[i].staged = DSHOT_PIO_FRAME_STOP;
	}
	k_spin_unlock(&data->frame_lock, key);

	return dshot_pio_send(dev);
}

static int dshot_pio_send_command(const struct device *dev, uint32_t channels,
				  enum dshot_command cmd)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	struct dshot_command_info info;
	k_spinlock_key_t key;
	uint16_t frame;
	int ret = 0;

	if ((channels == 0U) || ((channels & ~BIT_MASK(config->num_channels)) != 0U)) {
		return -EINVAL;
	}
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	dshot_command_get_info(cmd, &info);
	frame = dshot_frame_encode((uint16_t)cmd, info.telemetry);

	k_mutex_lock(&data->lock, K_FOREVER);

	key = k_spin_lock(&data->frame_lock);
	if (dshot_pio_all_stopped(dev)) {
		data->command_active = true;
	} else {
		ret = -EBUSY;
	}
	k_spin_unlock(&data->frame_lock, key);

	for (uint8_t i = 0; i < config->num_channels; i++) {
		data->tx_frames[i] = ((channels & BIT(i)) != 0U) ? frame : DSHOT_PIO_FRAME_STOP;
	}

	/* A stop frame between repetitions would reset the ESC repeat count */
	for (uint8_t r = 0; (ret == 0) && (r < info.repeat); r++) {
		if (r > 0U) {
			k_usleep(DSHOT_COMMAND_INTERVAL_US);
		}
		ret = dshot_pio_transmit(dev);
	}

	if (data->command_active) {
		if (ret == 0) {
			k_usleep(info.delay_us);
		}
		key = k_spin_lock(&data->frame_lock);
		data->command_active = false;
		k_spin_unlock(&data->frame_lock, key);
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static uint8_t dshot_pio_get_channel_count(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;

	return config->num_channels;
}

static int dshot_pio_init(const struct device *dev)
{
	const struct dshot_pio_config *config = dev->config;
	struct dshot_pio_data *data = dev->data;
	uint8_t acquired;
	int ret = 0;

	k_mutex_init(&data->lock);

	for (acquired = 0; acquired < config->num_channels; acquired++) {
		const struct dshot_pio_channel_config *cc = &config->channels[acquired];

		if (!device_is_ready(cc->pio)) {
			LOG_ERR("%s not ready", cc->pio->name);
			ret = -ENODEV;
			break;
		}

		ret = pulse_io_channel_get(cc->pio, cc->index, &data->channels[acquired].handle);
		if (ret != 0) {
			LOG_ERR("cannot reserve %s channel %u (%d)", cc->pio->name, cc->index, ret);
			break;
		}
		data->channels[acquired].staged = DSHOT_PIO_FRAME_STOP;
	}

	if (ret == 0) {
		ret = dshot_pio_apply(dev, config->speed);
	}

	if (ret != 0) {
		while (acquired-- > 0U) {
			(void)pulse_io_channel_release(config->channels[acquired].pio,
						       data->channels[acquired].handle);
		}
	}

	return ret;
}

static DEVICE_API(dshot, dshot_pio_api) = {
	.configure = dshot_pio_configure,
	.set_frame = dshot_pio_set_frame,
	.send = dshot_pio_send,
	.stop = dshot_pio_stop,
	.send_command = dshot_pio_send_command,
	.get_channel_count = dshot_pio_get_channel_count,
};

#define DSHOT_PIO_CHANNEL_CONFIG(node_id, prop, idx)                                               \
	{                                                                                          \
		.pio = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),                       \
		.index = DT_PHA_BY_IDX(node_id, prop, idx, channel),                               \
	},

#define DSHOT_PIO_NUM_CHANNELS(inst) DT_INST_PROP_LEN(inst, pulse_ios)

#define DSHOT_PIO_DEFINE(inst)                                                                     \
	BUILD_ASSERT(DSHOT_PIO_NUM_CHANNELS(inst) <= 32,                                           \
		     "a DShot device drives at most 32 channels");                                 \
	static const struct dshot_pio_channel_config dshot_pio_channel_config_##inst[] = {         \
		DT_INST_FOREACH_PROP_ELEM(inst, pulse_ios, DSHOT_PIO_CHANNEL_CONFIG)};             \
	static struct dshot_pio_channel dshot_pio_channels_##inst[DSHOT_PIO_NUM_CHANNELS(inst)];   \
	static uint16_t dshot_pio_tx_frames_##inst[DSHOT_PIO_NUM_CHANNELS(inst)];                  \
	static const struct dshot_pio_config dshot_pio_config_##inst = {                           \
		.channels = dshot_pio_channel_config_##inst,                                       \
		.num_channels = DSHOT_PIO_NUM_CHANNELS(inst),                                      \
		.speed = DT_INST_PROP(inst, dshot_speed),                                          \
	};                                                                                         \
	static struct dshot_pio_data dshot_pio_data_##inst = {                                     \
		.channels = dshot_pio_channels_##inst,                                             \
		.tx_frames = dshot_pio_tx_frames_##inst,                                           \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, dshot_pio_init, NULL, &dshot_pio_data_##inst,                  \
			      &dshot_pio_config_##inst, POST_KERNEL, CONFIG_DSHOT_INIT_PRIORITY,   \
			      &dshot_pio_api);

DT_INST_FOREACH_STATUS_OKAY(DSHOT_PIO_DEFINE)
