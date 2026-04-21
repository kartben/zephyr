/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * DMIC driver for ESP32 family SoCs. Runs the I2S peripheral in PDM RX
 * mode and presents the stream through Zephyr's standard DMIC API.
 */

#define DT_DRV_COMPAT espressif_esp32_i2s_pdm

#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/devicetree.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <math.h>

#include <soc.h>
#include <esp_clk_tree.h>
#include <hal/i2s_hal.h>

#if SOC_GDMA_SUPPORTED
#include <zephyr/drivers/dma/dma_esp32.h>
#else
#include <soc/lldesc.h>
#include <esp_memory_utils.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>
#endif /* SOC_GDMA_SUPPORTED */

#if SOC_GDMA_SUPPORTED && !DT_HAS_COMPAT_STATUS_OKAY(espressif_esp32_gdma)
#error "DMA peripheral is not enabled! Enable the espressif,esp32-gdma node in your devicetree."
#endif /* SOC_GDMA_SUPPORTED */

LOG_MODULE_REGISTER(dmic_esp32, CONFIG_AUDIO_DMIC_LOG_LEVEL);

#define DMIC_ESP32_CLK_SRC I2S_CLK_SRC_DEFAULT
/*
 * PDM RX clock tree must match Espressif's PDM RX rules (see ESP-IDF
 * i2s_pdm_rx_calculate_clock): bclk = sample_rate * I2S_LL_PDM_BCK_FACTOR (64)
 * for I2S_PDM_DSR_8S, bclk_div >= I2S_PDM_RX_BCLK_DIV_MIN (8), mclk = bclk * bclk_div.
 * The previous implementation used mclk = rate * 128 and bclk_div = 2, which violates
 * the minimum bclk divisor and can yield wrong PDM/PCM (e.g. pegged levels).
 */
#define DMIC_ESP32_PDM_BCK_FACTOR_8S   64U
#define DMIC_ESP32_PDM_BCK_FACTOR_16S  128U
#define DMIC_ESP32_PDM_RX_BCLK_DIV_MIN 8U
#define DMIC_ESP32_HWV1_PLL_D2_CLK     80000000U
#define DMIC_ESP32_HWV1_PDM_BCK_DIV    2U
#define DMIC_ESP32_HWV1_PDM_CLK_FACTOR (DMIC_ESP32_PDM_BCK_FACTOR_16S)
#define DMIC_ESP32_DMA_MAX_CHUNK       4092U

struct dmic_esp32_queue_item {
	void *buffer;
	size_t size;
};

struct dmic_esp32_data {
	enum dmic_state state;
	struct k_mem_slab *mem_slab;
	uint32_t block_size;
	void *active_buf;
#if SOC_GDMA_SUPPORTED
	uint32_t chunk_idx;
	uint32_t chunks_rem;
	uint32_t chunk_len;
#endif
	struct k_msgq rx_queue;
#if !SOC_GDMA_SUPPORTED
	struct intr_handle_data_t *irq_handle;
#endif
};

struct dmic_esp32_cfg {
	int unit;
	i2s_hal_context_t hal;
	const struct pinctrl_dev_config *pcfg;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
#if SOC_GDMA_SUPPORTED
	const struct device *dma_dev;
	uint32_t dma_channel;
#else
	lldesc_t *dma_desc;
	int irq_source;
	int irq_priority;
	int irq_flags;
#endif
	char *queue_buffer;
	uint32_t queue_depth;
};

static int dmic_esp32_rx_arm(const struct device *dev, void *buffer);
static void dmic_esp32_hw_stop(const struct device *dev);

#if SOC_GDMA_SUPPORTED
static void dmic_esp32_gdma_prep_block(struct dmic_esp32_data *data)
{
	data->chunk_idx = 0U;
	data->chunk_len = MIN(data->block_size, DMIC_ESP32_DMA_MAX_CHUNK);
	data->chunks_rem = DIV_ROUND_UP(data->block_size, DMIC_ESP32_DMA_MAX_CHUNK) - 1U;
}

static void *dmic_esp32_gdma_chunk_buf(const struct dmic_esp32_data *data, void *buffer)
{
	return (uint8_t *)buffer + (data->chunk_idx * DMIC_ESP32_DMA_MAX_CHUNK);
}

static void dmic_esp32_gdma_advance_chunk(struct dmic_esp32_data *data)
{
	uint32_t remaining;

	data->chunk_idx++;
	data->chunks_rem--;
	remaining = data->block_size - (data->chunk_idx * DMIC_ESP32_DMA_MAX_CHUNK);
	data->chunk_len = MIN(remaining, DMIC_ESP32_DMA_MAX_CHUNK);
}
#endif /* SOC_GDMA_SUPPORTED */

static uint32_t dmic_esp32_u32_abs_diff(float a, float b)
{
	float diff = a - b;

	return (uint32_t)fabsf(diff);
}

static void dmic_esp32_calc_clk_div(uint32_t *div_a, uint32_t *div_b, uint32_t *div_n,
				    uint32_t base_clock, uint32_t target_freq)
{
	uint32_t save_n = 255U;
	uint32_t save_a = 63U;
	uint32_t save_b = 62U;

	if (base_clock <= (target_freq << 1)) {
		*div_n = 2U;
		*div_a = 1U;
		*div_b = 0U;
		return;
	}

	if (target_freq != 0U) {
		float fdiv = (float)base_clock / (float)target_freq;
		uint32_t n = (uint32_t)fdiv;

		if (n < 256U) {
			float check_base = (float)base_clock;
			float check_target;
			uint32_t save_diff = UINT32_MAX;

			fdiv -= n;

			while ((int32_t)target_freq >= 0) {
				target_freq <<= 1;
				check_base *= 2.0f;
			}
			check_target = (float)target_freq;

			if (n < 255U) {
				save_a = 1U;
				save_b = 0U;
				save_n = n + 1U;
				save_diff = dmic_esp32_u32_abs_diff(check_target,
								     check_base / (float)save_n);
			}

			for (uint32_t a = 1U; a < 64U; ++a) {
				uint32_t b = (uint32_t)lroundf((float)a * fdiv);
				uint32_t diff;

				if (a <= b) {
					continue;
				}

				diff = dmic_esp32_u32_abs_diff(check_target,
							 ((check_base * (float)a) /
							  (float)(n * a + b)));
				if (save_diff <= diff) {
					continue;
				}

				save_diff = diff;
				save_a = a;
				save_b = b;
				save_n = n;
				if (diff == 0U) {
					break;
				}
			}
		}
	}

	*div_n = save_n;
	*div_a = save_a;
	*div_b = save_b;
}

#if SOC_I2S_HW_VERSION_2
static uint32_t dmic_esp32_get_source_clk_freq(void)
{
	uint32_t clk_freq = 0;

	esp_clk_tree_src_get_freq_hz(DMIC_ESP32_CLK_SRC,
				     ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED, &clk_freq);
	return clk_freq;
}

static int dmic_esp32_calc_clock(const struct dmic_cfg *dev_config, uint32_t pcm_rate,
				 i2s_hal_clock_info_t *clk_info, i2s_pdm_dsr_t *pdm_dsr)
{
	const uint32_t min_pdm_clk = dev_config->io.min_pdm_clk_freq;
	const uint32_t max_pdm_clk = dev_config->io.max_pdm_clk_freq;
	uint32_t bclk = 0U;

	clk_info->sclk = dmic_esp32_get_source_clk_freq();

	if (max_pdm_clk != 0U && min_pdm_clk > max_pdm_clk) {
		LOG_ERR("Invalid PDM clock range: min=%u max=%u",
			(unsigned int)min_pdm_clk, (unsigned int)max_pdm_clk);
		return -EINVAL;
	}

	bclk = pcm_rate * DMIC_ESP32_PDM_BCK_FACTOR_8S;
	*pdm_dsr = I2S_PDM_DSR_8S;
	if ((min_pdm_clk != 0U && bclk < min_pdm_clk) ||
	    (max_pdm_clk != 0U && bclk > max_pdm_clk)) {
		uint32_t alt_bclk = pcm_rate * DMIC_ESP32_PDM_BCK_FACTOR_16S;

		if ((min_pdm_clk == 0U || alt_bclk >= min_pdm_clk) &&
		    (max_pdm_clk == 0U || alt_bclk <= max_pdm_clk)) {
			bclk = alt_bclk;
			*pdm_dsr = I2S_PDM_DSR_16S;
		} else {
			LOG_ERR("PCM rate %u cannot satisfy PDM clock range %u..%u Hz",
				(unsigned int)pcm_rate, (unsigned int)min_pdm_clk,
				(unsigned int)max_pdm_clk);
			return -EINVAL;
		}
	}

	clk_info->bclk = bclk;
	clk_info->bclk_div = DMIC_ESP32_PDM_RX_BCLK_DIV_MIN;
	clk_info->mclk = clk_info->bclk * clk_info->bclk_div;
	if (clk_info->mclk == 0) {
		return -EINVAL;
	}

	clk_info->mclk_div = clk_info->sclk / clk_info->mclk;
	if (clk_info->mclk_div == 0) {
		LOG_ERR("PCM rate %u too high for source clock %u",
			(unsigned int)pcm_rate, (unsigned int)clk_info->sclk);
		return -EINVAL;
	}

	return 0;
}
#endif

#if SOC_I2S_HW_VERSION_1
static int dmic_esp32_configure_clock_hwv1(const struct dmic_cfg *dev_config, uint32_t pcm_rate,
					   i2s_hal_context_t *hal)
{
	const uint32_t min_pdm_clk = dev_config->io.min_pdm_clk_freq;
	const uint32_t max_pdm_clk = dev_config->io.max_pdm_clk_freq;
	const uint32_t pdm_clk = pcm_rate * DMIC_ESP32_HWV1_PDM_CLK_FACTOR;
	uint32_t div_a;
	uint32_t div_b;
	uint32_t div_n;

	if (max_pdm_clk != 0U && min_pdm_clk > max_pdm_clk) {
		LOG_ERR("Invalid PDM clock range: min=%u max=%u",
			(unsigned int)min_pdm_clk, (unsigned int)max_pdm_clk);
		return -EINVAL;
	}

	if ((min_pdm_clk != 0U && pdm_clk < min_pdm_clk) ||
	    (max_pdm_clk != 0U && pdm_clk > max_pdm_clk)) {
		LOG_ERR("PCM rate %u yields unsupported PDM clock %u Hz (range %u..%u Hz)",
			(unsigned int)pcm_rate, (unsigned int)pdm_clk,
			(unsigned int)min_pdm_clk, (unsigned int)max_pdm_clk);
		return -EINVAL;
	}

	dmic_esp32_calc_clk_div(&div_a, &div_b, &div_n,
				DMIC_ESP32_HWV1_PLL_D2_CLK / DMIC_ESP32_HWV1_PDM_CLK_FACTOR,
				pcm_rate);

	hal->dev->sample_rate_conf.rx_bck_div_num = DMIC_ESP32_HWV1_PDM_BCK_DIV;
	hal->dev->clkm_conf.clkm_div_a = div_a;
	hal->dev->clkm_conf.clkm_div_b = div_b;
	hal->dev->clkm_conf.clkm_div_num = div_n;
	hal->dev->clkm_conf.clka_en = 0U;

	/* If RX is not reset here, BCK polarity can be inverted on classic ESP32. */
	hal->dev->conf.rx_reset = 1U;
	hal->dev->conf.rx_fifo_reset = 1U;
	hal->dev->conf.rx_reset = 0U;
	hal->dev->conf.rx_fifo_reset = 0U;

	LOG_INF("pcm_rate=%u sclk=%u pdm_clk=%u clkm_div=%u+(%u/%u) bck_div=%u dsr=16s",
		(unsigned int)pcm_rate, (unsigned int)DMIC_ESP32_HWV1_PLL_D2_CLK,
		(unsigned int)pdm_clk, (unsigned int)div_n,
		(unsigned int)div_b, (unsigned int)div_a,
		(unsigned int)DMIC_ESP32_HWV1_PDM_BCK_DIV);

	return 0;
}
#endif

static void dmic_esp32_queue_drop(const struct device *dev)
{
	struct dmic_esp32_data *data = dev->data;
	struct dmic_esp32_queue_item item;

	while (k_msgq_get(&data->rx_queue, &item, K_NO_WAIT) == 0) {
		k_mem_slab_free(data->mem_slab, item.buffer);
	}
}

#if SOC_GDMA_SUPPORTED

static int dmic_esp32_rx_reload(const struct device *dev, void *buffer)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	const i2s_hal_context_t *hal = &cfg->hal;
	void *chunk_buf = dmic_esp32_gdma_chunk_buf(data, buffer);
	int err;

	i2s_ll_rx_set_eof_num(hal->dev, data->chunk_len);

	err = dma_reload(cfg->dma_dev, cfg->dma_channel, 0, (uint32_t)chunk_buf, data->chunk_len);
	if (err < 0) {
		LOG_ERR("dma_reload failed: %d", err);
		return err;
	}

	err = dma_start(cfg->dma_dev, cfg->dma_channel);
	if (err < 0) {
		LOG_ERR("dma_start failed: %d", err);
		return err;
	}

	return 0;
}

static void dmic_esp32_dma_callback(const struct device *dma_dev, void *arg,
				    uint32_t channel, int status)
{
	const struct device *dev = arg;
	struct dmic_esp32_data *data = dev->data;
	struct dmic_esp32_queue_item item;
	void *next_buf = NULL;
	int err;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if (status < 0) {
		LOG_ERR("DMA error %d", status);
		data->state = DMIC_STATE_ERROR;
		dmic_esp32_hw_stop(dev);
		return;
	}

	if (data->state != DMIC_STATE_ACTIVE) {
		return;
	}

	if (data->chunks_rem > 0U) {
		dmic_esp32_gdma_advance_chunk(data);
		err = dmic_esp32_rx_reload(dev, data->active_buf);
		if (err < 0) {
			data->state = DMIC_STATE_ERROR;
			dmic_esp32_hw_stop(dev);
		}
		return;
	}

	item.buffer = data->active_buf;
	item.size = data->block_size;

	err = k_mem_slab_alloc(data->mem_slab, &next_buf, K_NO_WAIT);
	if (err < 0) {
		LOG_ERR("slab exhausted");
		data->state = DMIC_STATE_ERROR;
		dmic_esp32_hw_stop(dev);
		k_mem_slab_free(data->mem_slab, item.buffer);
		data->active_buf = NULL;
		return;
	}

	if (k_msgq_put(&data->rx_queue, &item, K_NO_WAIT) < 0) {
		LOG_WRN("RX queue full, dropping block");
		k_mem_slab_free(data->mem_slab, item.buffer);
	}

	data->active_buf = next_buf;
	dmic_esp32_gdma_prep_block(data);
	err = dmic_esp32_rx_reload(dev, next_buf);
	if (err < 0) {
		data->state = DMIC_STATE_ERROR;
		dmic_esp32_hw_stop(dev);
	}
}

static int dmic_esp32_rx_arm(const struct device *dev, void *buffer)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dma_config dma_cfg = {0};
	struct dma_block_config dma_blk = {0};
	struct dmic_esp32_data *data = dev->data;
	const i2s_hal_context_t *hal = &cfg->hal;
	void *chunk_buf = dmic_esp32_gdma_chunk_buf(data, buffer);
	int err;

	dma_blk.block_size = data->chunk_len;
	dma_blk.dest_address = (uint32_t)chunk_buf;

	dma_cfg.source_data_size = 4;
	dma_cfg.dest_data_size = 4;
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_callback = dmic_esp32_dma_callback;
	dma_cfg.user_data = (void *)dev;
	dma_cfg.dma_slot = (cfg->unit == 0) ? ESP_GDMA_TRIG_PERIPH_I2S0
					    : ESP_GDMA_TRIG_PERIPH_I2S1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	err = dma_config(cfg->dma_dev, cfg->dma_channel, &dma_cfg);
	if (err < 0) {
		LOG_ERR("dma_config failed: %d", err);
		return err;
	}

	i2s_ll_rx_set_eof_num(hal->dev, data->chunk_len);

	err = dma_start(cfg->dma_dev, cfg->dma_channel);
	if (err < 0) {
		LOG_ERR("dma_start failed: %d", err);
		return err;
	}

	return 0;
}

#else /* !SOC_GDMA_SUPPORTED */

static void dmic_esp32_rx_isr(void *arg);

static uint32_t dmic_esp32_build_desc(const struct device *dev, void *buffer)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	lldesc_t *desc = cfg->dma_desc;
	uint32_t remaining = data->block_size;
	uint32_t last_chunk = 0;
	uint8_t *p = (uint8_t *)buffer;

	while (remaining > 0) {
		uint32_t chunk = MIN(remaining, DMIC_ESP32_DMA_MAX_CHUNK);

		memset(desc, 0, sizeof(*desc));
		desc->owner = 1;
		desc->buf = p;
		desc->length = chunk;
		desc->size = chunk;
		remaining -= chunk;
		last_chunk = chunk;
		p += chunk;
		desc->empty = (remaining > 0) ? (uint32_t)(desc + 1) : 0;
		desc++;
	}

	return last_chunk;
}

static int dmic_esp32_rx_arm(const struct device *dev, void *buffer)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	const i2s_hal_context_t *hal = &cfg->hal;
	uint32_t last_chunk;

	if (!esp_ptr_dma_capable(buffer)) {
		LOG_ERR("buffer %p not DMA-capable", buffer);
		return -EINVAL;
	}

	last_chunk = dmic_esp32_build_desc(dev, buffer);

	i2s_hal_rx_reset(hal);
	i2s_hal_rx_reset_dma(hal);
	i2s_hal_rx_reset_fifo(hal);
	i2s_hal_clear_intr_status(hal, I2S_LL_RX_EVENT_MASK);
	i2s_ll_rx_set_eof_num(hal->dev, last_chunk);

	i2s_hal_rx_enable_dma(hal);
	i2s_hal_rx_enable_intr(hal);
	i2s_hal_rx_start_link(hal, (uint32_t)cfg->dma_desc);

	esp_intr_enable(data->irq_handle);

	return 0;
}

static int dmic_esp32_rx_reload(const struct device *dev, void *buffer)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	const i2s_hal_context_t *hal = &cfg->hal;
	uint32_t last_chunk;

	last_chunk = dmic_esp32_build_desc(dev, buffer);

	i2s_ll_rx_set_eof_num(hal->dev, last_chunk);
	i2s_hal_rx_start_link(hal, (uint32_t)cfg->dma_desc);

	return 0;
}

static void IRAM_ATTR dmic_esp32_rx_isr(void *arg)
{
	const struct device *dev = arg;
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	const i2s_hal_context_t *hal = &cfg->hal;
	uint32_t status = i2s_hal_get_intr_status(hal);
	struct dmic_esp32_queue_item item;
	void *next_buf = NULL;
	int err;

	i2s_hal_clear_intr_status(hal, status);

	if (!(status & I2S_LL_EVENT_RX_EOF)) {
		return;
	}

	if (data->state != DMIC_STATE_ACTIVE) {
		return;
	}

	if (status & I2S_LL_EVENT_RX_DSCR_ERR) {
		LOG_ERR("RX descriptor error");
		data->state = DMIC_STATE_ERROR;
		k_mem_slab_free(data->mem_slab, data->active_buf);
		data->active_buf = NULL;
		return;
	}

	item.buffer = data->active_buf;
	item.size = data->block_size;

	err = k_mem_slab_alloc(data->mem_slab, &next_buf, K_NO_WAIT);
	if (err < 0) {
		data->state = DMIC_STATE_ERROR;
		k_mem_slab_free(data->mem_slab, item.buffer);
		return;
	}

	if (k_msgq_put(&data->rx_queue, &item, K_NO_WAIT) < 0) {
		LOG_WRN("RX queue full, dropping block");
		k_mem_slab_free(data->mem_slab, item.buffer);
	}

	data->active_buf = next_buf;
	(void)dmic_esp32_rx_reload(dev, next_buf);
}

#endif /* SOC_GDMA_SUPPORTED */

static void dmic_esp32_hw_stop(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	const i2s_hal_context_t *hal = &cfg->hal;

	i2s_hal_rx_stop(hal);

#if SOC_GDMA_SUPPORTED
	dma_stop(cfg->dma_dev, cfg->dma_channel);
#else
	struct dmic_esp32_data *data = dev->data;

	esp_intr_disable(data->irq_handle);
	i2s_hal_rx_stop_link(hal);
	i2s_hal_rx_disable_intr(hal);
	i2s_hal_rx_disable_dma(hal);
	i2s_hal_clear_intr_status(hal, I2S_INTR_MAX);
#endif
}

static int dmic_esp32_configure(const struct device *dev, struct dmic_cfg *dev_config)
{
	struct dmic_esp32_data *data = dev->data;
	const struct dmic_esp32_cfg *cfg = dev->config;
	i2s_hal_context_t *hal = (i2s_hal_context_t *)&cfg->hal;
	struct pdm_chan_cfg *channel = &dev_config->channel;
	struct pcm_stream_cfg *stream = &dev_config->streams[0];
#if SOC_I2S_HW_VERSION_2
	i2s_hal_clock_info_t clk_info;
	i2s_pdm_dsr_t pdm_dsr;
#endif
	i2s_hal_slot_config_t slot_cfg = {0};
	int err;

	if (data->state == DMIC_STATE_ACTIVE) {
		LOG_ERR("Cannot configure while active");
		return -EBUSY;
	}

	if (channel->req_num_streams != 1) {
		LOG_ERR("Only a single stream is supported");
		return -EINVAL;
	}

	if (channel->req_num_chan < 1 || channel->req_num_chan > 2) {
		LOG_ERR("Unsupported channel count: %u", channel->req_num_chan);
		return -EINVAL;
	}

	if (stream->pcm_width != 16) {
		LOG_ERR("Only 16-bit PCM samples are supported");
		return -EINVAL;
	}

	if (stream->pcm_rate == 0 || stream->block_size == 0 || stream->mem_slab == NULL) {
		LOG_ERR("Invalid stream configuration");
		return -EINVAL;
	}

	slot_cfg.data_bit_width = 16;
	slot_cfg.slot_bit_width = 16;
	slot_cfg.slot_mode = (channel->req_num_chan == 1) ? I2S_SLOT_MODE_MONO
							  : I2S_SLOT_MODE_STEREO;
	slot_cfg.pdm_rx.data_fmt = I2S_PDM_DATA_FMT_PCM;

	/* Mono: honor Zephyr PDM L/R map (PDM mic SEL/LR pin picks the PDM time slot). */
	if (channel->req_num_chan == 1) {
		uint8_t pdm_idx;
		enum pdm_lr lr;

		dmic_parse_channel_map(channel->req_chan_map_lo, channel->req_chan_map_hi, 0,
				       &pdm_idx, &lr);
		slot_cfg.pdm_rx.slot_mask = (lr == PDM_CHAN_RIGHT) ? I2S_PDM_SLOT_RIGHT
								   : I2S_PDM_SLOT_LEFT;
		ARG_UNUSED(pdm_idx);
	} else {
		slot_cfg.pdm_rx.slot_mask = I2S_PDM_SLOT_BOTH;
	}
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
	slot_cfg.pdm_rx.hp_en = true;
	/* Cut-off frequency in Hz * 10 (HAL integer encoding; ~35.5 Hz). */
	slot_cfg.pdm_rx.hp_cut_off_freq_hzx10 = 355U;
	slot_cfg.pdm_rx.amplify_num = 1;
#endif

	/*
	 * Order of operations matches ESP-IDF's i2s_pdm_rx_init() flow:
	 *   1. Stop + reset RX/FIFO.
	 *   2. Set the RX clock FIRST. On HW_VERSION_2 (ESP32-S3/C3/...),
	 *      i2s_hal_set_rx_clock() is what calls _i2s_ll_rx_enable_clock() and
	 *      _i2s_ll_mclk_bind_to_rx_clk(). Until this runs the RX submodule is
	 *      ungated, so writes to rx_conf bits like rx_pdm_en/rx_pdm2pcm_en
	 *      do not latch. Doing PDM enable before this leaves the peripheral
	 *      in standard-I2S RX mode with a wildly wrong effective sample rate
	 *      (DMA fills at FIFO speed, not at PCM-rate).
	 *   3. Apply slot config (slot_mode, slot_mask, HP filter, etc).
	 *   4. Switch the RX datapath to PDM and enable the PDM-to-PCM filter.
	 *   5. Program the PDM decimator (DSR) so it matches bclk = rate * 64.
	 */
	i2s_hal_rx_stop(hal);
	i2s_hal_rx_reset(hal);
	i2s_hal_rx_reset_fifo(hal);

#if SOC_I2S_HW_VERSION_1
	i2s_hal_pdm_set_rx_slot(hal, false, &slot_cfg);
	i2s_ll_rx_enable_pdm(hal->dev, true);
	i2s_ll_rx_set_pdm_dsr(hal->dev, I2S_PDM_DSR_16S);
	err = dmic_esp32_configure_clock_hwv1(dev_config, stream->pcm_rate, hal);
	if (err < 0) {
		return err;
	}
#else
	err = dmic_esp32_calc_clock(dev_config, stream->pcm_rate, &clk_info, &pdm_dsr);
	if (err < 0) {
		return err;
	}

	LOG_INF("pcm_rate=%u sclk=%u mclk=%u bclk=%u mclk_div=%u bclk_div=%u dsr=%s",
		(unsigned int)stream->pcm_rate,
		(unsigned int)clk_info.sclk, (unsigned int)clk_info.mclk,
		(unsigned int)clk_info.bclk,
		(unsigned int)clk_info.mclk_div, (unsigned int)clk_info.bclk_div,
		(pdm_dsr == I2S_PDM_DSR_16S) ? "16s" : "8s");

	i2s_hal_set_rx_clock(hal, &clk_info, DMIC_ESP32_CLK_SRC, NULL);
	i2s_hal_pdm_set_rx_slot(hal, false, &slot_cfg);
	i2s_ll_rx_enable_pdm(hal->dev, true);
	i2s_ll_rx_set_pdm_dsr(hal->dev, pdm_dsr);
#endif

#if SOC_I2S_HW_VERSION_2
	/* Commit configuration to the peripheral and dump the latched register
	 * state so we can verify rx_pdm_en / rx_pdm2pcm_en / rx_clk_active
	 * actually took effect.
	 */
	i2s_ll_rx_update(hal->dev);
	LOG_INF("after configure: rx_pdm_en=%u rx_pdm2pcm_en=%u rx_tdm_en=%u "
		"rx_start=%u rx_pdm_sinc_dsr_16_en=%u",
		(unsigned int)hal->dev->rx_conf.rx_pdm_en,
		(unsigned int)hal->dev->rx_conf.rx_pdm2pcm_en,
		(unsigned int)hal->dev->rx_conf.rx_tdm_en,
		(unsigned int)hal->dev->rx_conf.rx_start,
		(unsigned int)hal->dev->rx_conf.rx_pdm_sinc_dsr_16_en);
	LOG_INF("  rx_clk_active=%u rx_clk_sel=%u mclk_sel=%u",
		(unsigned int)hal->dev->rx_clkm_conf.rx_clk_active,
		(unsigned int)hal->dev->rx_clkm_conf.rx_clk_sel,
		(unsigned int)hal->dev->rx_clkm_conf.mclk_sel);
#endif

	data->mem_slab = stream->mem_slab;
	data->block_size = stream->block_size;

	channel->act_num_streams = 1;
	channel->act_num_chan = channel->req_num_chan;
	channel->act_chan_map_lo = channel->req_chan_map_lo;
	channel->act_chan_map_hi = channel->req_chan_map_hi;

	data->state = DMIC_STATE_CONFIGURED;

	return 0;
}

static int dmic_esp32_start(const struct device *dev)
{
	struct dmic_esp32_data *data = dev->data;
	const struct dmic_esp32_cfg *cfg = dev->config;
	const i2s_hal_context_t *hal = &cfg->hal;
	void *buf = NULL;
	int err;

	err = k_mem_slab_alloc(data->mem_slab, &buf, K_NO_WAIT);
	if (err < 0) {
		LOG_ERR("initial slab alloc failed: %d", err);
		return err;
	}

	data->active_buf = buf;
#if SOC_GDMA_SUPPORTED
	dmic_esp32_gdma_prep_block(data);
#endif

	err = dmic_esp32_rx_arm(dev, buf);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, buf);
		data->active_buf = NULL;
		return err;
	}

	i2s_hal_rx_start(hal);

	data->state = DMIC_STATE_ACTIVE;
	return 0;
}

static int dmic_esp32_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	struct dmic_esp32_data *data = dev->data;

	switch (cmd) {
	case DMIC_TRIGGER_START:
	case DMIC_TRIGGER_RELEASE:
		if (data->state == DMIC_STATE_ACTIVE) {
			return 0;
		}
		if (data->state != DMIC_STATE_CONFIGURED && data->state != DMIC_STATE_PAUSED) {
			LOG_ERR("start from invalid state: %d", data->state);
			return -EIO;
		}
		return dmic_esp32_start(dev);

	case DMIC_TRIGGER_PAUSE:
		if (data->state == DMIC_STATE_ACTIVE) {
			data->state = DMIC_STATE_PAUSED;
			dmic_esp32_hw_stop(dev);
		}
		return 0;

	case DMIC_TRIGGER_STOP:
	case DMIC_TRIGGER_RESET:
		if (data->state == DMIC_STATE_ACTIVE || data->state == DMIC_STATE_PAUSED ||
		    data->state == DMIC_STATE_ERROR) {
			data->state = DMIC_STATE_CONFIGURED;
			dmic_esp32_hw_stop(dev);
		}
		if (data->active_buf != NULL) {
			k_mem_slab_free(data->mem_slab, data->active_buf);
			data->active_buf = NULL;
		}
#if SOC_GDMA_SUPPORTED
		data->chunk_idx = 0U;
		data->chunks_rem = 0U;
		data->chunk_len = 0U;
#endif
		dmic_esp32_queue_drop(dev);
		data->state = (cmd == DMIC_TRIGGER_RESET) ? DMIC_STATE_INITIALIZED
							  : DMIC_STATE_CONFIGURED;
		return 0;

	default:
		LOG_ERR("invalid trigger: %d", cmd);
		return -EINVAL;
	}
}

static int dmic_esp32_read(const struct device *dev, uint8_t stream_id,
			   void **buffer, size_t *size, int32_t timeout)
{
	struct dmic_esp32_data *data = dev->data;
	struct dmic_esp32_queue_item item;
	int err;

	ARG_UNUSED(stream_id);

	if (data->state != DMIC_STATE_ACTIVE && data->state != DMIC_STATE_PAUSED &&
	    data->state != DMIC_STATE_ERROR) {
		LOG_ERR("read in invalid state: %d", data->state);
		return -EIO;
	}

	err = k_msgq_get(&data->rx_queue, &item, SYS_TIMEOUT_MS(timeout));
	if (err < 0) {
		return err;
	}

	*buffer = item.buffer;
	*size = item.size;
	return 0;
}

static int dmic_esp32_init(const struct device *dev)
{
	struct dmic_esp32_data *data = dev->data;
	const struct dmic_esp32_cfg *cfg = dev->config;
	int err;

	if (!device_is_ready(cfg->clock_dev)) {
		LOG_ERR("clock device not ready");
		return -ENODEV;
	}

	err = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	if (err != 0) {
		LOG_ERR("clock_on failed: %d", err);
		return -EIO;
	}

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		LOG_ERR("pinctrl_apply_state failed: %d", err);
		return -EIO;
	}

	k_msgq_init(&data->rx_queue, cfg->queue_buffer,
		    sizeof(struct dmic_esp32_queue_item), cfg->queue_depth);

#if SOC_GDMA_SUPPORTED
	if (!device_is_ready(cfg->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}
#else
	int irq_flags = ESP_PRIO_TO_FLAGS(cfg->irq_priority) |
			ESP_INT_FLAGS_CHECK(cfg->irq_flags) | ESP_INTR_FLAG_IRAM |
			ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED;

	err = esp_intr_alloc_intrstatus(
		cfg->irq_source, irq_flags,
		(uint32_t)i2s_ll_get_intr_status_reg(cfg->hal.dev),
		I2S_LL_RX_EVENT_MASK, dmic_esp32_rx_isr, (void *)dev, &data->irq_handle);
	if (err != 0) {
		LOG_ERR("IRQ alloc failed: %d", err);
		return -EIO;
	}
	i2s_ll_clear_intr_status(cfg->hal.dev, I2S_INTR_MAX);
#endif

	data->state = DMIC_STATE_INITIALIZED;
	return 0;
}

static const struct _dmic_ops dmic_esp32_ops = {
	.configure = dmic_esp32_configure,
	.trigger = dmic_esp32_trigger,
	.read = dmic_esp32_read,
};

#if SOC_GDMA_SUPPORTED

#define DMIC_ESP32_SANITY(n)                                                                       \
	BUILD_ASSERT(DT_INST_DMAS_HAS_NAME(n, rx),                                                 \
		     "espressif,esp32-i2s-pdm instance " #n " requires dmas/dma-names=rx")

#define DMIC_ESP32_DMA_DECLARE(n)
#define DMIC_ESP32_DMA_INIT(n)                                                                     \
	.dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(n, rx)),                                \
	.dma_channel = DT_INST_DMAS_CELL_BY_NAME(n, rx, channel),

#else /* !SOC_GDMA_SUPPORTED */

#define DMIC_ESP32_SANITY(n)                                                                       \
	BUILD_ASSERT(!DT_INST_NODE_HAS_PROP(n, dmas),                                              \
		     "espressif,esp32-i2s-pdm instance " #n                                        \
		     " must not declare dmas on this SoC")

/* Worst-case descriptor count for the biggest expected block size. */
#define DMIC_ESP32_DESC_COUNT 8

#define DMIC_ESP32_DMA_DECLARE(n)                                                                  \
	static lldesc_t dmic_esp32_dma_desc_##n[DMIC_ESP32_DESC_COUNT];

#define DMIC_ESP32_DMA_INIT(n)                                                                     \
	.dma_desc = dmic_esp32_dma_desc_##n,                                                       \
	.irq_source = DT_INST_IRQN(n),                                                             \
	.irq_priority = DT_INST_IRQ(n, priority),                                                  \
	.irq_flags = DT_INST_IRQ(n, flags),

#endif /* SOC_GDMA_SUPPORTED */

#define DMIC_ESP32_INIT(n)                                                                         \
	DMIC_ESP32_SANITY(n);                                                                      \
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	DMIC_ESP32_DMA_DECLARE(n)                                                                  \
	static char __aligned(4) dmic_esp32_queue_buf_##n                                          \
		[CONFIG_AUDIO_DMIC_ESP32_RX_BLOCK_COUNT *                                          \
		 sizeof(struct dmic_esp32_queue_item)];                                            \
	static struct dmic_esp32_data dmic_esp32_data_##n = {                                      \
		.state = DMIC_STATE_UNINIT,                                                        \
	};                                                                                         \
	static const struct dmic_esp32_cfg dmic_esp32_cfg_##n = {                                  \
		.unit = DT_INST_PROP(n, unit),                                                     \
		.hal = {.dev = (i2s_dev_t *)DT_INST_REG_ADDR(n)},                                  \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                         \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, offset),            \
		.queue_buffer = dmic_esp32_queue_buf_##n,                                          \
		.queue_depth = CONFIG_AUDIO_DMIC_ESP32_RX_BLOCK_COUNT,                             \
		DMIC_ESP32_DMA_INIT(n)                                                             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, dmic_esp32_init, NULL, &dmic_esp32_data_##n,                      \
			      &dmic_esp32_cfg_##n, POST_KERNEL,                                    \
			      CONFIG_AUDIO_DMIC_INIT_PRIORITY, &dmic_esp32_ops);

DT_INST_FOREACH_STATUS_OKAY(DMIC_ESP32_INIT)
