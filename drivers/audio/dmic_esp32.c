/*
 * Copyright (c) 2026 Espressif Systems (Shanghai) PTE LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT espressif_esp32_dmic

#include <errno.h>
#include <stdint.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(dmic_esp32, CONFIG_AUDIO_DMIC_LOG_LEVEL);

struct dmic_esp32_config {
const struct device *i2s_dev;
};

struct dmic_esp32_data {
struct pcm_stream_cfg stream;
};

static int dmic_esp32_configure(const struct device *dev, struct dmic_cfg *config)
{
const struct dmic_esp32_config *cfg = dev->config;
struct dmic_esp32_data *data = dev->data;
const struct pcm_stream_cfg *stream = &config->streams[0];
struct i2s_config i2s_cfg;

if (config->channel.req_num_streams != 1) {
LOG_ERR("only single stream is supported");
return -EINVAL;
}

if (config->channel.req_num_chan < 1 || config->channel.req_num_chan > 2) {
LOG_ERR("only 1 or 2 channels are supported");
return -EINVAL;
}

if (__builtin_popcount(config->channel.req_chan_map_lo) != config->channel.req_num_chan ||
    config->channel.req_chan_map_hi != 0U) {
LOG_ERR("invalid channel map requested");
return -EINVAL;
}

if ((stream->pcm_width != 16U) || (stream->block_size == 0U) ||
    ((stream->block_size % ((stream->pcm_width / 8U) * config->channel.req_num_chan)) != 0U) ||
    (stream->mem_slab == NULL)) {
LOG_ERR("unsupported stream configuration");
return -EINVAL;
}

i2s_cfg.word_size = stream->pcm_width;
i2s_cfg.channels = config->channel.req_num_chan;
i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
i2s_cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
i2s_cfg.frame_clk_freq = stream->pcm_rate;
i2s_cfg.mem_slab = stream->mem_slab;
i2s_cfg.block_size = stream->block_size;
i2s_cfg.timeout = SYS_FOREVER_MS;

if (i2s_configure(cfg->i2s_dev, I2S_DIR_RX, &i2s_cfg) < 0) {
LOG_ERR("failed to configure I2S RX stream");
return -EIO;
}

config->channel.act_num_streams = 1U;
config->channel.act_num_chan = config->channel.req_num_chan;
config->channel.act_chan_map_lo = config->channel.req_chan_map_lo;
config->channel.act_chan_map_hi = 0U;

data->stream = *stream;

return 0;
}

static int dmic_esp32_trigger(const struct device *dev, enum dmic_trigger cmd)
{
const struct dmic_esp32_config *cfg = dev->config;
enum i2s_trigger_cmd i2s_cmd;

switch (cmd) {
case DMIC_TRIGGER_START:
i2s_cmd = I2S_TRIGGER_START;
break;
case DMIC_TRIGGER_STOP:
case DMIC_TRIGGER_PAUSE:
i2s_cmd = I2S_TRIGGER_STOP;
break;
case DMIC_TRIGGER_RELEASE:
i2s_cmd = I2S_TRIGGER_START;
break;
case DMIC_TRIGGER_RESET:
i2s_cmd = I2S_TRIGGER_DROP;
break;
default:
return -EINVAL;
}

return i2s_trigger(cfg->i2s_dev, I2S_DIR_RX, i2s_cmd);
}

static int dmic_esp32_read(const struct device *dev, uint8_t stream,
   void **buffer, size_t *size, int32_t timeout)
{
const struct dmic_esp32_config *cfg = dev->config;

ARG_UNUSED(timeout);

if (stream != 0U) {
return -EINVAL;
}

return i2s_read(cfg->i2s_dev, buffer, size);
}

static int dmic_esp32_init(const struct device *dev)
{
const struct dmic_esp32_config *cfg = dev->config;

if (!device_is_ready(cfg->i2s_dev)) {
return -ENODEV;
}

return 0;
}

static const struct _dmic_ops dmic_esp32_ops = {
.configure = dmic_esp32_configure,
.trigger = dmic_esp32_trigger,
.read = dmic_esp32_read,
};

#define DMIC_ESP32_INIT(inst) \
BUILD_ASSERT(DT_INST_ON_BUS(inst, i2s), \
     "espressif,esp32-dmic must be on an I2S bus"); \
static struct dmic_esp32_data dmic_esp32_data_##inst; \
static const struct dmic_esp32_config dmic_esp32_cfg_##inst = { \
.i2s_dev = DEVICE_DT_GET(DT_INST_BUS(inst)), \
}; \
DEVICE_DT_INST_DEFINE(inst, dmic_esp32_init, NULL, \
      &dmic_esp32_data_##inst, &dmic_esp32_cfg_##inst, \
      POST_KERNEL, CONFIG_AUDIO_DMIC_INIT_PRIORITY, \
      &dmic_esp32_ops);

DT_INST_FOREACH_STATUS_OKAY(DMIC_ESP32_INIT)
