/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT i2s_buzzer

#include <limits.h>

#include <zephyr/drivers/buzzer.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(buzzer_i2s, CONFIG_BUZZER_LOG_LEVEL);

#define BUZZER_I2S_BLOCK_FRAMES    64U
#define BUZZER_I2S_BLOCK_COUNT     4U
#define BUZZER_I2S_BLOCK_SIZE      \
	(BUZZER_I2S_BLOCK_FRAMES * 2U * sizeof(int16_t))
#define BUZZER_I2S_DEFAULT_FREQ_HZ 1000U
#define BUZZER_I2S_MAX_FREQ_HZ     (UINT32_MAX / 2U)
#define BUZZER_I2S_TIMEOUT_MS      0

struct buzzer_i2s_config {
	const struct device *i2s;
	struct k_mem_slab *mem_slab;
};

struct buzzer_i2s_data {
	struct k_mutex lock;
	struct k_work_delayable refill_work;
	struct k_work_delayable stop_work;
	const struct device *dev;
	int16_t block[BUZZER_I2S_BLOCK_SIZE / sizeof(int16_t)];
	uint32_t freq_hz;
	uint32_t current_request_id;
	uint32_t request_id;
	uint8_t volume_percent;
	bool phase_high;
	bool running;
	bool stream_active;
};

static int buzzer_i2s_drop(const struct device *dev)
{
	const struct buzzer_i2s_config *cfg = dev->config;
	int ret;

	ret = i2s_trigger(cfg->i2s, I2S_DIR_TX, I2S_TRIGGER_DROP);
	if (ret == -EIO) {
		return 0;
	}

	return ret;
}

static int buzzer_i2s_configure_stream(const struct device *dev, uint32_t freq_hz)
{
	const struct buzzer_i2s_config *cfg = dev->config;
	struct i2s_config i2s_cfg = { 0 };

	if (freq_hz > BUZZER_I2S_MAX_FREQ_HZ) {
		return -EINVAL;
	}

	i2s_cfg.word_size = 16U;
	i2s_cfg.channels = 2U;
	i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
	i2s_cfg.options = I2S_OPT_BIT_CLK_CONTROLLER |
			  I2S_OPT_FRAME_CLK_CONTROLLER;
	i2s_cfg.frame_clk_freq = freq_hz * 2U;
	i2s_cfg.mem_slab = cfg->mem_slab;
	i2s_cfg.block_size = BUZZER_I2S_BLOCK_SIZE;
	i2s_cfg.timeout = BUZZER_I2S_TIMEOUT_MS;

	return i2s_configure(cfg->i2s, I2S_DIR_TX, &i2s_cfg);
}

static int16_t buzzer_i2s_amplitude(uint8_t volume_percent)
{
	return (int16_t)(((uint32_t)INT16_MAX * volume_percent) /
			 BUZZER_VOLUME_MAX);
}

static void buzzer_i2s_fill_block(int16_t *block, int16_t amplitude,
				  bool *phase_high)
{
	for (size_t i = 0U; i < BUZZER_I2S_BLOCK_FRAMES; i++) {
		int16_t sample = *phase_high ? amplitude : -amplitude;

		block[2U * i] = sample;
		block[(2U * i) + 1U] = sample;
		*phase_high = !*phase_high;
	}
}

static uint32_t buzzer_i2s_block_period_us(uint32_t freq_hz)
{
	return DIV_ROUND_UP(BUZZER_I2S_BLOCK_FRAMES * USEC_PER_SEC,
			    freq_hz * 2U);
}

static int buzzer_i2s_queue_audio(const struct device *dev, int16_t amplitude,
				  int16_t *block, bool *phase_high,
				  size_t max_blocks, size_t *queued_blocks)
{
	const struct buzzer_i2s_config *cfg = dev->config;

	*queued_blocks = 0U;

	for (size_t i = 0U; i < max_blocks; i++) {
		int ret;

		buzzer_i2s_fill_block(block, amplitude, phase_high);
		ret = i2s_buf_write(cfg->i2s, block, sizeof(block));
		if (ret == -EBUSY) {
			return 0;
		}
		if (ret < 0) {
			return ret;
		}

		(*queued_blocks)++;
	}

	return 0;
}

static void buzzer_i2s_stop_work(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	struct buzzer_i2s_data *data =
		CONTAINER_OF(dw, struct buzzer_i2s_data, stop_work);

	k_mutex_lock(&data->lock, K_FOREVER);
	data->stream_active = false;
	data->request_id++;
	k_mutex_unlock(&data->lock);

	k_work_reschedule(&data->refill_work, K_NO_WAIT);
}

static void buzzer_i2s_refill_work(struct k_work *work)
{
	struct k_work_delayable *dw = k_work_delayable_from_work(work);
	struct buzzer_i2s_data *data =
		CONTAINER_OF(dw, struct buzzer_i2s_data, refill_work);
	const struct device *dev = data->dev;
	const struct buzzer_i2s_config *cfg = dev->config;
	uint32_t freq_hz;
	uint32_t worker_request_id;
	uint32_t current_request_id;
	uint8_t volume_percent;
	bool stream_active;
	bool running;
	bool phase_high;
	bool schedule_next;
	int16_t amplitude;
	size_t queued_blocks;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	freq_hz = data->freq_hz;
	worker_request_id = data->current_request_id;
	current_request_id = data->request_id;
	volume_percent = data->volume_percent;
	stream_active = data->stream_active;
	running = data->running;
	phase_high = data->phase_high;
	k_mutex_unlock(&data->lock);

	amplitude = buzzer_i2s_amplitude(volume_percent);
	schedule_next = false;

	if (!stream_active || (freq_hz == BUZZER_FREQ_REST) ||
	    (volume_percent == 0U)) {
		if (running) {
			ret = buzzer_i2s_drop(dev);
			if (ret < 0) {
				LOG_ERR("Failed to stop I2S buzzer (%d)", ret);
			}
		}

		k_mutex_lock(&data->lock, K_FOREVER);
		data->running = false;
		data->phase_high = true;
		k_mutex_unlock(&data->lock);
		return;
	}

	if (!running || (current_request_id != worker_request_id)) {
		if (running) {
			ret = buzzer_i2s_drop(dev);
			if (ret < 0) {
				LOG_ERR("Failed to restart I2S buzzer (%d)", ret);
				k_mutex_lock(&data->lock, K_FOREVER);
				data->running = false;
				k_mutex_unlock(&data->lock);
				return;
			}
		}

		ret = buzzer_i2s_configure_stream(dev, freq_hz);
		if (ret < 0) {
			LOG_ERR("Failed to configure I2S buzzer (%d)", ret);
			k_mutex_lock(&data->lock, K_FOREVER);
			data->running = false;
			k_mutex_unlock(&data->lock);
			return;
		}

		phase_high = true;
		ret = buzzer_i2s_queue_audio(dev, amplitude, data->block,
					     &phase_high, 2U, &queued_blocks);
		if (ret < 0) {
			LOG_ERR("Failed to queue buzzer audio (%d)", ret);
			(void)buzzer_i2s_drop(dev);
			k_mutex_lock(&data->lock, K_FOREVER);
			data->running = false;
			k_mutex_unlock(&data->lock);
			return;
		}
		if (queued_blocks < 2U) {
			(void)buzzer_i2s_drop(dev);
			k_mutex_lock(&data->lock, K_FOREVER);
			data->running = false;
			k_mutex_unlock(&data->lock);
			return;
		}

		ret = i2s_trigger(cfg->i2s, I2S_DIR_TX, I2S_TRIGGER_START);
		if (ret < 0) {
			LOG_ERR("Failed to start I2S buzzer (%d)", ret);
			(void)buzzer_i2s_drop(dev);
			k_mutex_lock(&data->lock, K_FOREVER);
			data->running = false;
			k_mutex_unlock(&data->lock);
			return;
		}

		worker_request_id = current_request_id;

		k_mutex_lock(&data->lock, K_FOREVER);
		data->current_request_id = worker_request_id;
		data->running = true;
		data->phase_high = phase_high;
		k_mutex_unlock(&data->lock);
	}

	ret = buzzer_i2s_queue_audio(dev, amplitude, data->block, &phase_high,
				     BUZZER_I2S_BLOCK_COUNT, &queued_blocks);
	if (ret < 0) {
		k_mutex_lock(&data->lock, K_FOREVER);
		stream_active = data->stream_active;
		current_request_id = data->request_id;
		worker_request_id = data->current_request_id;
		k_mutex_unlock(&data->lock);

		if (stream_active && (current_request_id == worker_request_id)) {
			LOG_ERR("Failed to queue buzzer audio (%d)", ret);
		}

		(void)buzzer_i2s_drop(dev);

		k_mutex_lock(&data->lock, K_FOREVER);
		data->running = false;
		k_mutex_unlock(&data->lock);
		return;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->stream_active && (data->request_id == worker_request_id)) {
		data->phase_high = phase_high;
		schedule_next = true;
	}
	k_mutex_unlock(&data->lock);

	if (schedule_next) {
		k_work_reschedule(&data->refill_work,
				  K_USEC(buzzer_i2s_block_period_us(freq_hz)));
	}
}

static void buzzer_i2s_cancel_stop(struct buzzer_i2s_data *data)
{
	struct k_work_sync sync;

	k_work_cancel_delayable_sync(&data->stop_work, &sync);
}

static int buzzer_i2s_tone(const struct device *dev, uint32_t freq_hz,
			   uint32_t duration_ms)
{
	const struct buzzer_i2s_config *cfg = dev->config;
	struct buzzer_i2s_data *data = dev->data;
	bool stream_active;
	int ret;

	if (!device_is_ready(cfg->i2s)) {
		return -ENODEV;
	}

	if (freq_hz > BUZZER_I2S_MAX_FREQ_HZ) {
		return -EINVAL;
	}

	buzzer_i2s_cancel_stop(data);

	k_mutex_lock(&data->lock, K_FOREVER);
	data->freq_hz = freq_hz;
	stream_active = (freq_hz != BUZZER_FREQ_REST) &&
			(data->volume_percent != 0U);
	data->stream_active = stream_active;
	data->request_id++;
	k_mutex_unlock(&data->lock);

	if (stream_active && (duration_ms != BUZZER_DURATION_FOREVER)) {
		ret = k_work_reschedule(&data->stop_work, K_MSEC(duration_ms));
		if (ret < 0) {
			k_mutex_lock(&data->lock, K_FOREVER);
			data->stream_active = false;
			data->request_id++;
			k_mutex_unlock(&data->lock);
			k_work_reschedule(&data->refill_work, K_NO_WAIT);
			return ret;
		}
	}

	k_work_reschedule(&data->refill_work, K_NO_WAIT);

	return 0;
}

static int buzzer_i2s_set_volume(const struct device *dev, uint8_t percent)
{
	struct buzzer_i2s_data *data = dev->data;
	bool restart_stream;

	if (percent > BUZZER_VOLUME_MAX) {
		return -EINVAL;
	}

	restart_stream = false;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->volume_percent = percent;

	if (percent == 0U) {
		data->stream_active = false;
		data->request_id++;
		restart_stream = true;
	} else if (data->stream_active) {
		data->request_id++;
		restart_stream = true;
	}

	k_mutex_unlock(&data->lock);

	if (percent == 0U) {
		buzzer_i2s_cancel_stop(data);
	}

	if (restart_stream) {
		k_work_reschedule(&data->refill_work, K_NO_WAIT);
	}

	return 0;
}

static int buzzer_i2s_beep(const struct device *dev, uint32_t duration_ms)
{
	return buzzer_i2s_tone(dev, BUZZER_I2S_DEFAULT_FREQ_HZ, duration_ms);
}

static int buzzer_i2s_stop(const struct device *dev)
{
	struct buzzer_i2s_data *data = dev->data;

	buzzer_i2s_cancel_stop(data);

	k_mutex_lock(&data->lock, K_FOREVER);
	data->stream_active = false;
	data->request_id++;
	k_mutex_unlock(&data->lock);

	k_work_reschedule(&data->refill_work, K_NO_WAIT);

	return 0;
}

static int buzzer_i2s_init(const struct device *dev)
{
	const struct buzzer_i2s_config *cfg = dev->config;
	struct buzzer_i2s_data *data = dev->data;

	if (!device_is_ready(cfg->i2s)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->i2s);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->refill_work, buzzer_i2s_refill_work);
	k_work_init_delayable(&data->stop_work, buzzer_i2s_stop_work);
	data->dev = dev;
	data->freq_hz = BUZZER_FREQ_REST;
	data->current_request_id = 0U;
	data->request_id = 0U;
	data->volume_percent = BUZZER_VOLUME_MAX;
	data->phase_high = true;
	data->running = false;
	data->stream_active = false;

	return 0;
}

static DEVICE_API(buzzer, buzzer_i2s_api) = {
	.tone = buzzer_i2s_tone,
	.set_volume = buzzer_i2s_set_volume,
	.beep = buzzer_i2s_beep,
	.stop = buzzer_i2s_stop,
};

#define BUZZER_I2S_INIT(inst) \
	K_MEM_SLAB_DEFINE_STATIC(buzzer_i2s_slab_##inst, BUZZER_I2S_BLOCK_SIZE, \
				 BUZZER_I2S_BLOCK_COUNT, 4); \
	static const struct buzzer_i2s_config buzzer_i2s_cfg_##inst = { \
		.i2s = DEVICE_DT_GET(DT_INST_BUS(inst)), \
		.mem_slab = &buzzer_i2s_slab_##inst, \
	}; \
	static struct buzzer_i2s_data buzzer_i2s_data_##inst; \
	DEVICE_DT_INST_DEFINE(inst, buzzer_i2s_init, NULL, \
			      &buzzer_i2s_data_##inst, &buzzer_i2s_cfg_##inst, \
			      POST_KERNEL, CONFIG_BUZZER_INIT_PRIORITY, \
			      &buzzer_i2s_api);

DT_INST_FOREACH_STATUS_OKAY(BUZZER_I2S_INIT)
