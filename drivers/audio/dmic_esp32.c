/*
 * Copyright (c) 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT espressif_esp32_i2s_pdm_dmic

#include <zephyr/audio/dmic.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <soc.h>
#include <esp_clk_tree.h>
#include <hal/i2s_hal.h>

#if SOC_GDMA_SUPPORTED
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_esp32.h>
#else
#include <soc/lldesc.h>
#include <esp_memory_utils.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>
#endif

LOG_MODULE_REGISTER(dmic_esp32, CONFIG_AUDIO_DMIC_LOG_LEVEL);

#define DMIC_ESP32_CLK_SRC              I2S_CLK_SRC_DEFAULT
#define DMIC_ESP32_PDM_RX_BCLK_DIV_MIN  8U
#define DMIC_ESP32_PDM_RX_CLK_LIMIT     128U
#define DMIC_ESP32_DMA_BUFFER_MAX_SIZE  4092

struct dmic_esp32_data {
	struct k_mem_slab *mem_slab;
	uint32_t block_size;
	struct k_msgq rx_queue;
	void *rx_buffer;
	size_t rx_buffer_len;
	bool configured;
	bool active;
	bool stopping;
	bool dma_pending;
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
};

static uint32_t dmic_esp32_get_source_clk_freq(i2s_clock_src_t clk_src)
{
	uint32_t clk_freq = 0;

	esp_clk_tree_src_get_freq_hz(clk_src, ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,
				     &clk_freq);
	return clk_freq;
}

static int dmic_esp32_select_pdm_clock(uint32_t pcm_rate, i2s_pdm_slot_mask_t slot_mask,
				       const struct pdm_io_cfg *io, i2s_hal_clock_info_t *clk_info,
				       i2s_pdm_dsr_t *dsr)
{
	static const i2s_pdm_dsr_t dsr_opts[] = { I2S_PDM_DSR_8S, I2S_PDM_DSR_16S };
	uint32_t slot_num = __builtin_popcount(slot_mask);

	for (size_t i = 0; i < ARRAY_SIZE(dsr_opts); i++) {
		uint32_t dn_sample_factor = I2S_LL_PDM_BCK_FACTOR *
					    (dsr_opts[i] == I2S_PDM_DSR_16S ? 2U : 1U);
		uint32_t pdm_clk = pcm_rate * dn_sample_factor;
		uint32_t bclk_limit;

		if (pdm_clk < io->min_pdm_clk_freq || pdm_clk > io->max_pdm_clk_freq) {
			continue;
		}

		bclk_limit = (DMIC_ESP32_PDM_RX_CLK_LIMIT * slot_num + dn_sample_factor - 1U) /
			     dn_sample_factor;

		*dsr = dsr_opts[i];
		clk_info->bclk = pdm_clk;
		clk_info->bclk_div = MAX(bclk_limit, DMIC_ESP32_PDM_RX_BCLK_DIV_MIN);
		clk_info->mclk = clk_info->bclk * clk_info->bclk_div;
		clk_info->sclk = dmic_esp32_get_source_clk_freq(DMIC_ESP32_CLK_SRC);

		if ((float)clk_info->sclk <= (float)clk_info->mclk * 1.99f) {
			continue;
		}

		return 0;
	}

	return -EINVAL;
}

static i2s_pdm_slot_mask_t dmic_esp32_slot_mask_from_cfg(const struct pdm_chan_cfg *channel)
{
	uint8_t pdm;
	enum pdm_lr lr;

	if (channel->req_num_chan == 1) {
		dmic_parse_channel_map(channel->req_chan_map_lo, channel->req_chan_map_hi,
				       0, &pdm, &lr);
		return lr == PDM_CHAN_RIGHT ? I2S_PDM_SLOT_RIGHT : I2S_PDM_SLOT_LEFT;
	}

	return I2S_PDM_SLOT_BOTH;
}

static int dmic_esp32_hw_configure(const struct device *dev, struct dmic_cfg *config)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	i2s_hal_context_t *hal = (i2s_hal_context_t *)&cfg->hal;
	struct pcm_stream_cfg *stream = &config->streams[0];
	struct pdm_chan_cfg *channel = &config->channel;
	i2s_hal_slot_config_t slot_cfg = { 0 };
	i2s_hal_clock_info_t clk_info;
	i2s_pdm_dsr_t dsr = I2S_PDM_DSR_8S;
	int err;

	slot_cfg.pdm_rx.slot_mask = dmic_esp32_slot_mask_from_cfg(channel);

	err = dmic_esp32_select_pdm_clock(stream->pcm_rate, slot_cfg.pdm_rx.slot_mask,
					  &config->io, &clk_info, &dsr);
	if (err < 0) {
		LOG_ERR("PDM clock out of range");
		return err;
	}

	slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
	slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
	slot_cfg.slot_mode = channel->req_num_chan == 1 ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
	slot_cfg.pdm_rx.data_fmt = I2S_PDM_DATA_FMT_PCM;
#if SOC_I2S_SUPPORTS_PDM_RX_HP_FILTER
	slot_cfg.pdm_rx.hp_en = false;
	slot_cfg.pdm_rx.hp_cut_off_freq_hzx10 = 0;
	slot_cfg.pdm_rx.amplify_num = 1;
	if (stream->gain_db > 0) {
		slot_cfg.pdm_rx.amplify_num = MIN(15, 1 + (stream->gain_db / 6));
	}
#endif

	i2s_hal_rx_stop(hal);
	i2s_hal_rx_reset(hal);
#if !SOC_GDMA_SUPPORTED
	i2s_hal_rx_reset_dma(hal);
#endif
	i2s_hal_rx_reset_fifo(hal);

	i2s_hal_pdm_set_rx_slot(hal, false, &slot_cfg);
	i2s_hal_set_rx_clock(hal, &clk_info, DMIC_ESP32_CLK_SRC, NULL);
	i2s_ll_rx_set_pdm_dsr(hal->dev, dsr);
	i2s_hal_pdm_enable_rx_channel(hal, true);

	channel->act_num_streams = 1;
	channel->act_num_chan = channel->req_num_chan;
	channel->act_chan_map_lo = channel->req_chan_map_lo;
	channel->act_chan_map_hi = channel->req_chan_map_hi;

	return 0;
}

#if SOC_GDMA_SUPPORTED

static void dmic_esp32_rx_dma_callback(const struct device *dma_dev, void *arg,
				       uint32_t channel, int status)
{
	const struct device *dev = arg;
	struct dmic_esp32_data *data = dev->data;
	int err;

	ARG_UNUSED(dma_dev);
	ARG_UNUSED(channel);

	if (!data->dma_pending) {
		return;
	}

	data->dma_pending = false;

	if (status < 0 || data->rx_buffer == NULL) {
		LOG_ERR("RX DMA error: %d", status);
		return;
	}

	err = k_msgq_put(&data->rx_queue, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		LOG_ERR("RX queue full");
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return;
	}

	data->rx_buffer = NULL;

	if (!data->active || data->stopping) {
		return;
	}

	err = k_mem_slab_alloc(data->mem_slab, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		LOG_ERR("Failed to allocate RX buffer: %d", err);
		return;
	}

	data->rx_buffer_len = data->block_size;

	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dma_config dma_cfg = { 0 };
	struct dma_block_config dma_blk = { 0 };
	int ret;

	dma_blk.block_size = data->rx_buffer_len;
	dma_blk.dest_address = (uint32_t)data->rx_buffer;
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_callback = dmic_esp32_rx_dma_callback;
	dma_cfg.user_data = (void *)dev;
	dma_cfg.dma_slot = cfg->unit == 0 ? ESP_GDMA_TRIG_PERIPH_I2S0 : ESP_GDMA_TRIG_PERIPH_I2S1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	ret = dma_config(cfg->dma_dev, cfg->dma_channel, &dma_cfg);
	if (ret < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return;
	}

	i2s_ll_rx_set_eof_num(cfg->hal.dev, data->rx_buffer_len);
	ret = dma_start(cfg->dma_dev, cfg->dma_channel);
	if (ret < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return;
	}

	data->dma_pending = true;
}

static int dmic_esp32_start_dma_gdma(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	struct dma_config dma_cfg = { 0 };
	struct dma_block_config dma_blk = { 0 };
	int err;

	if (!device_is_ready(cfg->dma_dev)) {
		return -ENODEV;
	}

	err = k_mem_slab_alloc(data->mem_slab, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		return -ENOMEM;
	}

	data->rx_buffer_len = data->block_size;

	dma_blk.block_size = data->rx_buffer_len;
	dma_blk.dest_address = (uint32_t)data->rx_buffer;
	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_callback = dmic_esp32_rx_dma_callback;
	dma_cfg.user_data = (void *)dev;
	dma_cfg.dma_slot = cfg->unit == 0 ? ESP_GDMA_TRIG_PERIPH_I2S0 : ESP_GDMA_TRIG_PERIPH_I2S1;
	dma_cfg.block_count = 1;
	dma_cfg.head_block = &dma_blk;

	err = dma_config(cfg->dma_dev, cfg->dma_channel, &dma_cfg);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return err;
	}

	i2s_hal_rx_enable_dma(&cfg->hal);
	i2s_hal_rx_enable_intr(&cfg->hal);
	i2s_ll_rx_set_eof_num(cfg->hal.dev, data->rx_buffer_len);

	err = dma_start(cfg->dma_dev, cfg->dma_channel);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return err;
	}

	data->dma_pending = true;

	return 0;
}

static void dmic_esp32_stop_dma_gdma(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;

	dma_stop(cfg->dma_dev, cfg->dma_channel);
	i2s_hal_rx_disable_intr(&cfg->hal);
	i2s_hal_rx_disable_dma(&cfg->hal);
	i2s_hal_clear_intr_status(&cfg->hal, I2S_INTR_MAX);

	if (data->rx_buffer != NULL) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
	}

	data->dma_pending = false;
}

#else /* !SOC_GDMA_SUPPORTED */

static int dmic_esp32_config_dma_legacy(const struct device *dev);
static void dmic_esp32_rx_done(const struct device *dev, uint32_t status);

static void IRAM_ATTR dmic_esp32_rx_handler(void *arg)
{
	const struct device *dev = arg;
	const struct dmic_esp32_cfg *cfg = dev->config;
	uint32_t intr_status = i2s_hal_get_intr_status(&cfg->hal);

	i2s_hal_clear_intr_status(&cfg->hal, intr_status);
	if (intr_status & I2S_LL_EVENT_RX_EOF) {
		dmic_esp32_rx_done(dev, intr_status);
	}
}

static void dmic_esp32_rx_done(const struct device *dev, uint32_t status)
{
	struct dmic_esp32_data *data = dev->data;
	const struct dmic_esp32_cfg *cfg = dev->config;
	int err;

	if (!data->dma_pending) {
		return;
	}

	data->dma_pending = false;

	if ((status & I2S_LL_EVENT_RX_DSCR_ERR) || data->rx_buffer == NULL) {
		LOG_ERR("RX error");
		return;
	}

	err = k_msgq_put(&data->rx_queue, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return;
	}

	data->rx_buffer = NULL;

	if (!data->active || data->stopping) {
		return;
	}

	err = k_mem_slab_alloc(data->mem_slab, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		return;
	}

	data->rx_buffer_len = data->block_size;

	err = dmic_esp32_config_dma_legacy(dev);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return;
	}

	i2s_hal_rx_reset_fifo(&cfg->hal);
	i2s_hal_rx_start_link(&cfg->hal, (uint32_t)cfg->dma_desc);
	i2s_ll_rx_set_eof_num(cfg->hal.dev, data->rx_buffer_len);
	data->dma_pending = true;
}

static int dmic_esp32_config_dma_legacy(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	uint32_t mem_block = (uint32_t)data->rx_buffer;
	uint32_t mem_block_size = data->rx_buffer_len;
	lldesc_t *desc_iter = cfg->dma_desc;

	if (!esp_ptr_dma_capable(data->rx_buffer)) {
		return -EINVAL;
	}

	for (int i = 0; i < CONFIG_AUDIO_DMIC_ESP32_DMA_DESC_NUM_MAX; i++) {
		uint32_t buffer_size;

		if (mem_block_size > DMIC_ESP32_DMA_BUFFER_MAX_SIZE) {
			buffer_size = DMIC_ESP32_DMA_BUFFER_MAX_SIZE;
		} else {
			buffer_size = mem_block_size;
		}

		memset(desc_iter, 0, sizeof(lldesc_t));
		desc_iter->owner = 1;
		desc_iter->buf = (uint8_t *)mem_block;
		desc_iter->length = buffer_size;
		desc_iter->size = buffer_size;
		desc_iter->eof = 0;
		desc_iter->empty = 0;

		mem_block += buffer_size;
		mem_block_size -= buffer_size;

		if (mem_block_size > 0) {
			desc_iter->empty = (uint32_t)(desc_iter + 1);
			desc_iter++;
		} else {
			break;
		}
	}

	if (mem_block_size > 0) {
		return -EINVAL;
	}

	return 0;
}

static int dmic_esp32_start_dma_legacy(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	int err;

	err = k_mem_slab_alloc(data->mem_slab, &data->rx_buffer, K_NO_WAIT);
	if (err < 0) {
		return -ENOMEM;
	}

	data->rx_buffer_len = data->block_size;

	err = dmic_esp32_config_dma_legacy(dev);
	if (err < 0) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
		return err;
	}

	i2s_hal_rx_reset_dma(&cfg->hal);
	i2s_hal_rx_reset_fifo(&cfg->hal);
	i2s_hal_pdm_enable_rx_channel((i2s_hal_context_t *)&cfg->hal, true);

	i2s_hal_rx_enable_dma(&cfg->hal);
	i2s_hal_rx_enable_intr(&cfg->hal);
	i2s_hal_rx_start_link(&cfg->hal, (uint32_t)cfg->dma_desc);
	i2s_ll_rx_set_eof_num(cfg->hal.dev, data->rx_buffer_len);

	esp_intr_enable(data->irq_handle);
	data->dma_pending = true;

	return 0;
}

static void dmic_esp32_stop_dma_legacy(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;

	esp_intr_disable(data->irq_handle);
	i2s_hal_rx_stop_link(&cfg->hal);
	i2s_hal_rx_disable_intr(&cfg->hal);
	i2s_hal_rx_disable_dma(&cfg->hal);
	i2s_hal_clear_intr_status(&cfg->hal, I2S_INTR_MAX);

	if (data->rx_buffer != NULL) {
		k_mem_slab_free(data->mem_slab, data->rx_buffer);
		data->rx_buffer = NULL;
	}

	data->dma_pending = false;
}

#endif /* SOC_GDMA_SUPPORTED */

static int dmic_esp32_configure(const struct device *dev, struct dmic_cfg *config)
{
	struct dmic_esp32_data *data = dev->data;
	struct pdm_chan_cfg *channel = &config->channel;
	struct pcm_stream_cfg *stream = &config->streams[0];
	uint32_t def_map, alt_map;
	int err;

	if (data->active) {
		return -EBUSY;
	}

	if (channel->req_num_streams != 1 || channel->req_num_chan < 1 ||
	    channel->req_num_chan > 2) {
		return -EINVAL;
	}

	if (channel->req_num_chan == 1) {
		def_map = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
		alt_map = dmic_build_channel_map(0, 0, PDM_CHAN_RIGHT);
	} else {
		def_map = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT) |
			  dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
		alt_map = dmic_build_channel_map(0, 0, PDM_CHAN_RIGHT) |
			  dmic_build_channel_map(1, 0, PDM_CHAN_LEFT);
	}

	if (channel->req_chan_map_lo != def_map && channel->req_chan_map_lo != alt_map) {
		return -EINVAL;
	}

	if (stream->pcm_rate == 0 || stream->pcm_width == 0) {
		if (data->configured) {
			const struct dmic_esp32_cfg *cfg = dev->config;

			i2s_hal_rx_stop(&cfg->hal);
			data->configured = false;
		}
		return 0;
	}

	if (stream->pcm_width != 16) {
		return -EINVAL;
	}

	if (stream->pcm_rate < 8000 || stream->pcm_rate > 48000) {
		return -EINVAL;
	}

	if (stream->mem_slab == NULL || stream->block_size == 0) {
		return -EINVAL;
	}

	err = dmic_esp32_hw_configure(dev, config);
	if (err < 0) {
		return err;
	}

	data->block_size = stream->block_size;
	data->mem_slab = stream->mem_slab;
	data->configured = true;

	return 0;
}

static int dmic_esp32_start(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	int err;

	err = pinctrl_apply_state(cfg->pcfg, PINCTRL_STATE_DEFAULT);
	if (err < 0) {
		return err;
	}

#if SOC_GDMA_SUPPORTED
	err = dmic_esp32_start_dma_gdma(dev);
#else
	err = dmic_esp32_start_dma_legacy(dev);
#endif
	if (err < 0) {
		return err;
	}

	i2s_hal_rx_start(&cfg->hal);

	return 0;
}

static int dmic_esp32_stop(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;

	i2s_hal_rx_stop(&cfg->hal);

#if SOC_GDMA_SUPPORTED
	dmic_esp32_stop_dma_gdma(dev);
#else
	dmic_esp32_stop_dma_legacy(dev);
#endif

	data->active = false;
	data->stopping = false;

	return 0;
}

static int dmic_esp32_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	struct dmic_esp32_data *data = dev->data;

	switch (cmd) {
	case DMIC_TRIGGER_STOP:
		if (data->active) {
			data->stopping = true;
			return dmic_esp32_stop(dev);
		}
		break;
	case DMIC_TRIGGER_START:
		if (!data->configured) {
			return -EIO;
		}
		if (!data->active) {
			data->stopping = false;
			data->active = true;
			return dmic_esp32_start(dev);
		}
		break;
	case DMIC_TRIGGER_PAUSE:
	case DMIC_TRIGGER_RELEASE:
	case DMIC_TRIGGER_RESET:
		return -ENOSYS;
	default:
		return -EINVAL;
	}

	return 0;
}

static int dmic_esp32_read(const struct device *dev, uint8_t stream, void **buffer,
			    size_t *size, int32_t timeout)
{
	struct dmic_esp32_data *data = dev->data;
	int ret;

	ARG_UNUSED(stream);

	if (!data->configured) {
		return -EIO;
	}

	ret = k_msgq_get(&data->rx_queue, buffer, SYS_TIMEOUT_MS(timeout));
	if (ret != 0) {
		return ret;
	}

	*size = data->block_size;

	return 0;
}

static int dmic_esp32_init(const struct device *dev)
{
	const struct dmic_esp32_cfg *cfg = dev->config;
	struct dmic_esp32_data *data = dev->data;
	int err;

	if (!device_is_ready(cfg->clock_dev)) {
		return -ENODEV;
	}

#if SOC_GDMA_SUPPORTED
	if (!device_is_ready(cfg->dma_dev)) {
		return -ENODEV;
	}
#endif

	err = clock_control_on(cfg->clock_dev, cfg->clock_subsys);
	if (err != 0) {
		return -EIO;
	}

	i2s_ll_enable_core_clock(cfg->hal.dev, true);

#if !SOC_GDMA_SUPPORTED
	int irq_flags = ESP_PRIO_TO_FLAGS(cfg->irq_priority) |
			ESP_INT_FLAGS_CHECK(cfg->irq_flags) | ESP_INTR_FLAG_IRAM |
			ESP_INTR_FLAG_INTRDISABLED | ESP_INTR_FLAG_SHARED;

	err = esp_intr_alloc_intrstatus(cfg->irq_source, irq_flags,
					(uint32_t)i2s_ll_get_intr_status_reg(cfg->hal.dev),
					I2S_LL_RX_EVENT_MASK, dmic_esp32_rx_handler, (void *)dev,
					&data->irq_handle);
	if (err != 0) {
		return err;
	}
#endif

	return 0;
}

static DEVICE_API(dmic, dmic_esp32_api) = {
	.configure = dmic_esp32_configure,
	.trigger = dmic_esp32_trigger,
	.read = dmic_esp32_read,
};

#if SOC_GDMA_SUPPORTED

#define DMIC_ESP32_DMA_ASSERT(inst)                                                                \
	BUILD_ASSERT(DT_INST_NODE_HAS_PROP(inst, dmas), "dmas required for GDMA SoCs");            \
	BUILD_ASSERT(DT_INST_DMAS_HAS_NAME(inst, rx), "dma-names must include rx");

#define DMIC_ESP32_LEGACY_DMA_DESC(inst)

#else /* !SOC_GDMA_SUPPORTED */

#define DMIC_ESP32_DMA_ASSERT(inst)

#define DMIC_ESP32_LEGACY_DMA_DESC(inst)                                                           \
	lldesc_t dmic_esp32_dma_desc_##inst[CONFIG_AUDIO_DMIC_ESP32_DMA_DESC_NUM_MAX];

#endif /* SOC_GDMA_SUPPORTED */

#define DMIC_ESP32_CFG_EXTRA(inst)                                                                 \
	COND_CODE_1(IS_ENABLED(SOC_GDMA_SUPPORTED),                                                  \
		    (, .dma_dev = DEVICE_DT_GET(DT_INST_DMAS_CTLR_BY_NAME(inst, rx)),                 \
		     .dma_channel = DT_INST_DMAS_CELL_BY_NAME(inst, rx, channel)),                   \
		    (, .irq_source = DT_INST_IRQ_BY_NAME(inst, rx, irq),                             \
		     .irq_priority = DT_INST_IRQ_BY_NAME(inst, rx, priority),                      \
		     .irq_flags = DT_INST_IRQ_BY_NAME(inst, rx, flags),                            \
		     .dma_desc = dmic_esp32_dma_desc_##inst))

#define DMIC_ESP32_INIT(inst)                                                                      \
	DMIC_ESP32_DMA_ASSERT(inst)                                                                \
	IF_ENABLED(SOC_I2S_HW_VERSION_2,                                                           \
		   (BUILD_ASSERT(DT_INST_PROP(inst, unit) == 0,                                      \
				 "PDM2PCM only supported on I2S unit 0")))                      \
	DMIC_ESP32_LEGACY_DMA_DESC(inst)                                                           \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
	static void *dmic_esp32_rx_msgs_##inst[DT_INST_PROP(inst, queue_size)];                    \
	static struct dmic_esp32_data dmic_esp32_data_##inst;                                      \
	static const struct dmic_esp32_cfg dmic_esp32_cfg_##inst = {                                 \
		.unit = DT_INST_PROP(inst, unit),                                                   \
		.hal = {.dev = (i2s_dev_t *)DT_INST_REG_ADDR(inst)},                               \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(inst)),                             \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(inst, offset)         \
		DMIC_ESP32_CFG_EXTRA(inst)};                                                       \
	static int dmic_esp32_init_##inst(const struct device *dev)                                  \
	{                                                                                          \
		struct dmic_esp32_data *data = dev->data;                                          \
		int ret = dmic_esp32_init(dev);                                                    \
		if (ret < 0) {                                                                     \
			return ret;                                                              \
		}                                                                                  \
		k_msgq_init(&data->rx_queue, (char *)dmic_esp32_rx_msgs_##inst,                    \
			    sizeof(void *), ARRAY_SIZE(dmic_esp32_rx_msgs_##inst));                \
		return 0;                                                                          \
	}                                                                                          \
	DEVICE_DT_INST_DEFINE(inst, dmic_esp32_init_##inst, NULL, &dmic_esp32_data_##inst,         \
			      &dmic_esp32_cfg_##inst, POST_KERNEL, CONFIG_AUDIO_DMIC_INIT_PRIORITY,  \
			      &dmic_esp32_api);

DT_INST_FOREACH_STATUS_OKAY(DMIC_ESP32_INIT)
