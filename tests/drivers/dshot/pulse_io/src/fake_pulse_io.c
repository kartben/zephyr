/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT vnd_pulse_io_fake

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>

#include "fake_pulse_io.h"

#define FAKE_PIO_SRC_CLK_HZ 80000000U

struct fake_pio_data {
	struct pulse_io_caps caps;
	struct fake_pio_groups groups;
	uint32_t src_clk_hz;
	bool readback;
	bool group_support;
	uint8_t num_channels;
	struct fake_pio_channel channels[FAKE_PIO_MAX_CHANNELS];
};

/* Like the ESP32-S3 RMT: 80 MHz source, 15-bit fields, 4 TX channels */
static void fake_pio_default_caps(struct fake_pio_data *data)
{
	data->caps = (struct pulse_io_caps){
		.modes = PULSE_IO_MODE_SYMBOL | PULSE_IO_MODE_CELL,
		.supports_tx = true,
		.supports_rx = false,
		.min_tick_ns = 13,
		.max_tick_ns = 819200,
		.max_duration_ticks = 32767,
		.tx_channel_mask = BIT_MASK(data->num_channels),
		.num_channels = data->num_channels,
		.cell_duty_bits = 15,
		.tx_max_streaming = SIZE_MAX,
	};
	data->caps.tx_sync_multi_channel = data->group_support;
	data->src_clk_hz = FAKE_PIO_SRC_CLK_HZ;
	data->readback = true;
}

static struct fake_pio_channel *to_channel(const struct device *dev,
					   struct pulse_io_channel *handle)
{
	struct fake_pio_data *data = dev->data;
	uintptr_t idx = (uintptr_t)handle - 1U;

	if (idx >= data->num_channels) {
		return NULL;
	}
	return &data->channels[idx];
}

struct fake_pio_channel *fake_pio_channel(const struct device *dev, uint8_t index)
{
	struct fake_pio_data *data = dev->data;

	return &data->channels[index];
}

struct pulse_io_caps *fake_pio_caps(const struct device *dev)
{
	struct fake_pio_data *data = dev->data;

	return &data->caps;
}

void fake_pio_set_src_clock(const struct device *dev, uint32_t src_clk_hz)
{
	struct fake_pio_data *data = dev->data;

	data->src_clk_hz = src_clk_hz;
}

void fake_pio_set_readback(const struct device *dev, bool supported)
{
	struct fake_pio_data *data = dev->data;

	data->readback = supported;
}

struct fake_pio_groups *fake_pio_groups(const struct device *dev)
{
	struct fake_pio_data *data = dev->data;

	return &data->groups;
}

void fake_pio_reset(const struct device *dev)
{
	struct fake_pio_data *data = dev->data;

	fake_pio_default_caps(data);
	data->groups = (struct fake_pio_groups){0};
	for (uint8_t i = 0; i < data->num_channels; i++) {
		struct fake_pio_channel *ch = &data->channels[i];

		ch->configure_count = 0;
		ch->tx_count = 0;
		ch->last_count = 0;
		ch->history_len = 0;
		ch->tx_error = 0;
		ch->tx_gate = NULL;
		ch->tx_waiting = false;
	}
}

static int fake_get_capabilities(const struct device *dev, struct pulse_io_caps *caps)
{
	struct fake_pio_data *data = dev->data;

	*caps = data->caps;
	return 0;
}

static int fake_channel_get(const struct device *dev, uint8_t channel_idx,
			    struct pulse_io_channel **chan)
{
	struct fake_pio_data *data = dev->data;

	if (channel_idx >= data->num_channels) {
		return -ENODEV;
	}
	if (data->channels[channel_idx].in_use) {
		return -EBUSY;
	}
	data->channels[channel_idx].in_use = true;
	*chan = (struct pulse_io_channel *)(uintptr_t)(channel_idx + 1U);
	return 0;
}

static int fake_channel_release(const struct device *dev, struct pulse_io_channel *chan)
{
	struct fake_pio_channel *ch = to_channel(dev, chan);

	if ((ch == NULL) || !ch->in_use) {
		return -EINVAL;
	}
	ch->in_use = false;
	ch->configured = false;
	return 0;
}

static int fake_channel_configure(const struct device *dev, struct pulse_io_channel *chan,
				  const struct pulse_io_config *cfg)
{
	struct fake_pio_data *data = dev->data;
	struct fake_pio_channel *ch = to_channel(dev, chan);
	uint32_t div;

	if ((ch == NULL) || !ch->in_use) {
		return -EINVAL;
	}
	if ((cfg->mode & data->caps.modes) == 0U) {
		return -ENOTSUP;
	}
	if ((cfg->mode == PULSE_IO_MODE_CELL) &&
	    ((cfg->cell_period_ticks == 0U) ||
	     (cfg->cell_period_ticks > data->caps.max_duration_ticks))) {
		return -EINVAL;
	}
	if (cfg->resolution_hz == 0U) {
		return -EINVAL;
	}

	div = DIV_ROUND_CLOSEST(data->src_clk_hz, cfg->resolution_hz);
	ch->resolution_hz = data->src_clk_hz / MAX(div, 1U);
	ch->cfg = *cfg;
	ch->configured = true;
	ch->configure_count++;
	return 0;
}

static int fake_channel_get_resolution(const struct device *dev, struct pulse_io_channel *chan,
				       uint32_t *resolution_hz)
{
	struct fake_pio_data *data = dev->data;
	struct fake_pio_channel *ch = to_channel(dev, chan);

	if (!data->readback) {
		return -ENOSYS;
	}
	if ((ch == NULL) || !ch->configured) {
		return -EINVAL;
	}
	*resolution_hz = ch->resolution_hz;
	return 0;
}

static uint16_t fake_decode(const struct fake_pio_channel *ch)
{
	uint16_t bits = 0;

	for (size_t i = 0; i < 16U; i++) {
		uint32_t high;
		uint32_t period;

		if (ch->cfg.mode == PULSE_IO_MODE_CELL) {
			if (i >= ch->last_count) {
				break;
			}
			high = ch->last.cells[i].duty;
			period = ch->cfg.cell_period_ticks;
		} else {
			if ((2U * i + 1U) >= ch->last_count) {
				break;
			}
			high = ch->last.symbols[2U * i].duration;
			period = high + ch->last.symbols[2U * i + 1U].duration;
		}
		bits = (uint16_t)((bits << 1) | ((2U * high > period) ? 1U : 0U));
	}

	return bits;
}

static int fake_check_tx(struct fake_pio_channel *ch, const struct pulse_io_tx_req *req)
{
	if ((ch == NULL) || !ch->configured || (req->count == 0U)) {
		return -EINVAL;
	}
	if (req->count > FAKE_PIO_MAX_ENTRIES) {
		return -ENOMEM;
	}
	return 0;
}

static int fake_record_tx(struct fake_pio_channel *ch, const struct pulse_io_tx_req *req,
			  k_timeout_t timeout)
{
	if (ch->tx_gate != NULL) {
		ch->tx_waiting = true;
		k_sem_take(ch->tx_gate, K_FOREVER);
		ch->tx_waiting = false;
	}

	/* Read the buffer at the end, as hardware streaming it would */
	memcpy(ch->last.symbols, req->symbols, req->count * sizeof(struct pulse_symbol));
	ch->last_count = req->count;
	ch->last_timeout = timeout;
	ch->tx_count++;
	if (ch->history_len < FAKE_PIO_HISTORY) {
		ch->history[ch->history_len++] = fake_decode(ch);
	}

	return ch->tx_error;
}

static int fake_transmit_sync(const struct device *dev, struct pulse_io_channel *chan,
			      const struct pulse_io_tx_req *req, k_timeout_t timeout)
{
	struct fake_pio_channel *ch = to_channel(dev, chan);
	int ret = fake_check_tx(ch, req);

	return (ret != 0) ? ret : fake_record_tx(ch, req, timeout);
}

static int fake_transmit_group_sync(const struct device *dev,
				    const struct pulse_io_tx_group_entry *entries, size_t count,
				    uint32_t flags, k_timeout_t timeout)
{
	struct fake_pio_data *data = dev->data;
	int first_err = 0;

	if (((flags & PULSE_IO_TX_GROUP_SYNC) != 0U) && !data->caps.tx_sync_multi_channel) {
		return -ENOTSUP;
	}
	for (size_t i = 0; i < count; i++) {
		int ret = fake_check_tx(to_channel(dev, entries[i].chan), &entries[i].req);

		if (ret != 0) {
			return ret;
		}
	}

	data->groups.calls++;
	data->groups.last_count = count;
	data->groups.last_flags = flags;

	for (size_t i = 0; i < count; i++) {
		int ret = fake_record_tx(to_channel(dev, entries[i].chan), &entries[i].req,
					 timeout);

		if ((ret != 0) && (first_err == 0)) {
			first_err = ret;
		}
	}

	return first_err;
}

static int fake_receive_sync(const struct device *dev, struct pulse_io_channel *chan,
			     const struct pulse_io_rx_req *req, size_t *count, k_timeout_t timeout)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(chan);
	ARG_UNUSED(req);
	ARG_UNUSED(count);
	ARG_UNUSED(timeout);

	return -ENOTSUP;
}

static int fake_stop(const struct device *dev, struct pulse_io_channel *chan)
{
	return (to_channel(dev, chan) == NULL) ? -EINVAL : 0;
}

static DEVICE_API(pulse_io, fake_pio_api) = {
	.get_capabilities = fake_get_capabilities,
	.channel_get = fake_channel_get,
	.channel_release = fake_channel_release,
	.channel_configure = fake_channel_configure,
	.transmit_sync = fake_transmit_sync,
	.receive_sync = fake_receive_sync,
	.stop = fake_stop,
	.channel_get_resolution = fake_channel_get_resolution,
};

/* Same controller with group transmission */
static DEVICE_API(pulse_io, fake_pio_group_api) = {
	.get_capabilities = fake_get_capabilities,
	.channel_get = fake_channel_get,
	.channel_release = fake_channel_release,
	.channel_configure = fake_channel_configure,
	.transmit_sync = fake_transmit_sync,
	.receive_sync = fake_receive_sync,
	.stop = fake_stop,
	.channel_get_resolution = fake_channel_get_resolution,
	.transmit_group_sync = fake_transmit_group_sync,
};

static int fake_pio_init(const struct device *dev)
{
	fake_pio_default_caps(dev->data);
	return 0;
}

#define FAKE_PIO_DEFINE(inst)                                                                      \
	BUILD_ASSERT(DT_INST_PROP(inst, num_channels) <= FAKE_PIO_MAX_CHANNELS);                   \
	static struct fake_pio_data fake_pio_data_##inst = {                                       \
		.num_channels = DT_INST_PROP(inst, num_channels),                                  \
		.group_support = DT_INST_PROP(inst, group_transmit),                               \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, fake_pio_init, NULL, &fake_pio_data_##inst, NULL, POST_KERNEL, \
			      CONFIG_PULSE_IO_INIT_PRIORITY,                                       \
			      COND_CODE_1(DT_INST_PROP(inst, group_transmit),                      \
					  (&fake_pio_group_api), (&fake_pio_api)));

DT_INST_FOREACH_STATUS_OKAY(FAKE_PIO_DEFINE)
