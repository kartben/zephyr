/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ite_it8951

#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/it8951.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(it8951, CONFIG_DISPLAY_LOG_LEVEL);

/* SPI preambles */
#define IT8951_PREAMBLE_CMD  0x6000U
#define IT8951_PREAMBLE_WR   0x0000U
#define IT8951_PREAMBLE_RD   0x1000U

/* Built-in I80 commands */
#define IT8951_CMD_SYS_RUN   0x0001U
#define IT8951_CMD_STANDBY   0x0002U
#define IT8951_CMD_SLEEP     0x0003U
#define IT8951_CMD_REG_RD    0x0010U
#define IT8951_CMD_REG_WR    0x0011U
#define IT8951_CMD_LD_IMG_AREA 0x0021U
#define IT8951_CMD_LD_IMG_END  0x0022U

/* User-defined I80 commands */
#define IT8951_CMD_DPY_AREA      0x0034U
#define IT8951_CMD_VCOM_CTRL     0x0039U
#define IT8951_CMD_GET_DEV_INFO  0x0302U

/* Registers */
#define IT8951_REG_I80CPCR   0x0004U
#define IT8951_REG_LISAR_LO  0x0208U
#define IT8951_REG_LISAR_HI  0x020AU

/* LD_IMG_AREA arg encoding: (endian << 8) | (bpp << 4) | rotation */
#define IT8951_ENDIAN_BIG  1U
#define IT8951_BPP_2       0U
#define IT8951_BPP_3       1U
#define IT8951_BPP_4       2U
#define IT8951_BPP_8       3U

/* Sentinel for "vcom-mv unset". */
#define IT8951_VCOM_UNSET  INT16_MAX

/* Number of 16-bit words returned by GET_DEV_INFO. */
#define IT8951_DEV_INFO_WORDS 20

struct it8951_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec busy_gpio;
	uint16_t width;
	uint16_t height;
	int16_t  vcom_mv;
	uint8_t  default_waveform;
	uint8_t  pixel_format;       /* enum display_pixel_format */
	uint8_t  rotation;           /* 0..3, IT8951 hardware rotation */
	uint8_t *line_buf;
	size_t   line_buf_size;
};

struct it8951_data {
	uint32_t img_buf_addr;
	uint16_t panel_w;
	uint16_t panel_h;
	enum display_pixel_format pixel_format;
	enum display_orientation  orientation;
	uint8_t  waveform_mode;
	bool     blanking_on;
};

static int it8951_wait_busy(const struct device *dev)
{
	const struct it8951_config *cfg = dev->config;
	const int64_t deadline = k_uptime_get() + CONFIG_IT8951_BUSY_TIMEOUT_MS;

	while (gpio_pin_get_dt(&cfg->busy_gpio) == 0) {
		if (k_uptime_get() > deadline) {
			LOG_ERR("HRDY timeout");
			return -ETIMEDOUT;
		}
		k_usleep(100);
	}

	return 0;
}

/*
 * Send a single SPI transaction starting with a 16-bit preamble,
 * optionally followed by 16-bit words (sent big-endian on the wire),
 * optionally followed by raw bytes (already in wire order).
 */
static int it8951_spi_write(const struct device *dev, uint16_t preamble,
			    const uint16_t *words, size_t word_count,
			    const uint8_t *raw, size_t raw_len)
{
	const struct it8951_config *cfg = dev->config;
	uint8_t pre[2];
	uint8_t word_buf[16];
	struct spi_buf bufs[3];
	struct spi_buf_set tx;
	int ret;

	ret = it8951_wait_busy(dev);
	if (ret < 0) {
		return ret;
	}

	sys_put_be16(preamble, pre);
	bufs[0].buf = pre;
	bufs[0].len = sizeof(pre);
	tx.buffers = bufs;
	tx.count = 1;

	if (words != NULL && word_count > 0) {
		if (word_count * 2 > sizeof(word_buf)) {
			return -EINVAL;
		}
		for (size_t i = 0; i < word_count; i++) {
			sys_put_be16(words[i], &word_buf[i * 2]);
		}
		bufs[tx.count].buf = word_buf;
		bufs[tx.count].len = word_count * 2;
		tx.count++;
	}

	if (raw != NULL && raw_len > 0) {
		bufs[tx.count].buf = (void *)raw;
		bufs[tx.count].len = raw_len;
		tx.count++;
	}

	return spi_write_dt(&cfg->bus, &tx);
}

static int it8951_write_cmd(const struct device *dev, uint16_t cmd)
{
	return it8951_spi_write(dev, IT8951_PREAMBLE_CMD, &cmd, 1, NULL, 0);
}

static int it8951_write_args(const struct device *dev, const uint16_t *args, size_t count)
{
	return it8951_spi_write(dev, IT8951_PREAMBLE_WR, args, count, NULL, 0);
}

static int it8951_write_pixels(const struct device *dev, const uint8_t *raw, size_t len)
{
	return it8951_spi_write(dev, IT8951_PREAMBLE_WR, NULL, 0, raw, len);
}

/*
 * Read N 16-bit words after sending the read preamble + one dummy word.
 */
static int it8951_spi_read(const struct device *dev, uint16_t *out, size_t count)
{
	const struct it8951_config *cfg = dev->config;
	uint8_t header[4]; /* preamble + dummy */
	uint8_t rx[IT8951_DEV_INFO_WORDS * 2];
	struct spi_buf tx_buf = { .buf = header, .len = sizeof(header) };
	struct spi_buf rx_bufs[2] = {
		{ .buf = NULL, .len = sizeof(header) },
		{ .buf = rx, .len = count * 2 },
	};
	struct spi_buf_set tx = { .buffers = &tx_buf, .count = 1 };
	struct spi_buf_set rx_set = { .buffers = rx_bufs, .count = 2 };
	int ret;

	if (count * 2 > sizeof(rx)) {
		return -EINVAL;
	}

	ret = it8951_wait_busy(dev);
	if (ret < 0) {
		return ret;
	}

	sys_put_be16(IT8951_PREAMBLE_RD, &header[0]);
	sys_put_be16(0, &header[2]);

	ret = spi_transceive_dt(&cfg->bus, &tx, &rx_set);
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < count; i++) {
		out[i] = sys_get_be16(&rx[i * 2]);
	}

	return 0;
}

static int it8951_read_reg(const struct device *dev, uint16_t reg, uint16_t *val)
{
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_REG_RD);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_args(dev, &reg, 1);
	if (ret < 0) {
		return ret;
	}

	return it8951_spi_read(dev, val, 1);
}

static int it8951_write_reg(const struct device *dev, uint16_t reg, uint16_t val)
{
	uint16_t args[2] = { reg, val };
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_REG_WR);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_args(dev, args, ARRAY_SIZE(args));
}

static int it8951_set_target_addr(const struct device *dev, uint32_t addr)
{
	int ret;

	ret = it8951_write_reg(dev, IT8951_REG_LISAR_HI, (uint16_t)(addr >> 16));
	if (ret < 0) {
		return ret;
	}

	return it8951_write_reg(dev, IT8951_REG_LISAR_LO, (uint16_t)(addr & 0xFFFFU));
}

static int it8951_get_dev_info(const struct device *dev)
{
	struct it8951_data *data = dev->data;
	uint16_t info[IT8951_DEV_INFO_WORDS];
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_GET_DEV_INFO);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_spi_read(dev, info, IT8951_DEV_INFO_WORDS);
	if (ret < 0) {
		return ret;
	}

	data->panel_w = info[0];
	data->panel_h = info[1];
	data->img_buf_addr = ((uint32_t)info[3] << 16) | info[2];

	LOG_INF("IT8951 panel %ux%u, image buffer @ 0x%08x",
		data->panel_w, data->panel_h, data->img_buf_addr);

	return 0;
}

static int it8951_set_vcom(const struct device *dev, int16_t mv)
{
	/* VCOM is negative, the controller takes the absolute value in mV. */
	uint16_t args[2] = { 1U, (uint16_t)(mv < 0 ? -mv : mv) };
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_VCOM_CTRL);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_args(dev, args, ARRAY_SIZE(args));
}

static int it8951_load_img_area(const struct device *dev,
				uint16_t x, uint16_t y, uint16_t w, uint16_t h,
				uint8_t bpp_code, uint8_t rotation)
{
	uint16_t args[5] = {
		(uint16_t)((IT8951_ENDIAN_BIG << 8) | (bpp_code << 4) | rotation),
		x, y, w, h,
	};
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_LD_IMG_AREA);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_args(dev, args, ARRAY_SIZE(args));
}

static int it8951_load_img_end(const struct device *dev)
{
	return it8951_write_cmd(dev, IT8951_CMD_LD_IMG_END);
}

static int it8951_display_area(const struct device *dev,
			       uint16_t x, uint16_t y, uint16_t w, uint16_t h,
			       uint8_t mode)
{
	uint16_t args[5] = { x, y, w, h, mode };
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_DPY_AREA);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_args(dev, args, ARRAY_SIZE(args));
}

/* Pack one scanline of pixels into the on-the-wire 4bpp buffer. */
static size_t it8951_pack_line_l8(uint8_t *dst, const uint8_t *src, uint16_t width)
{
	size_t out = 0;

	for (uint16_t i = 0; i < width; i += 2U) {
		uint8_t a = src[i] & 0xF0U;
		uint8_t b = ((i + 1U) < width) ? (src[i + 1U] >> 4) : 0x0FU;

		dst[out++] = a | (b & 0x0FU);
	}

	return out;
}

static size_t it8951_pack_line_mono01(uint8_t *dst, const uint8_t *src, uint16_t width)
{
	size_t out = 0;

	for (uint16_t i = 0; i < width; i += 2U) {
		uint8_t bit_a = (src[i >> 3] >> (7U - (i & 7U))) & 0x1U;
		uint8_t pa = bit_a ? 0xF0U : 0x00U;
		uint8_t pb = 0x00U;

		if ((i + 1U) < width) {
			uint8_t bit_b = (src[(i + 1U) >> 3] >> (7U - ((i + 1U) & 7U))) & 0x1U;

			pb = bit_b ? 0x0FU : 0x00U;
		}

		dst[out++] = pa | pb;
	}

	return out;
}

static int it8951_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct it8951_config *cfg = dev->config;
	struct it8951_data *data = dev->data;
	const uint8_t *src = buf;
	uint16_t w = desc->width;
	uint16_t h = desc->height;
	size_t src_stride;
	int ret;

	if (buf == NULL || desc->buf_size == 0U) {
		return -EINVAL;
	}

	if ((uint32_t)x + w > data->panel_w || (uint32_t)y + h > data->panel_h) {
		LOG_ERR("write region (%u,%u,%ux%u) exceeds panel %ux%u",
			x, y, w, h, data->panel_w, data->panel_h);
		return -EINVAL;
	}

	if (data->pixel_format == PIXEL_FORMAT_L_8) {
		src_stride = desc->pitch;
	} else {
		src_stride = DIV_ROUND_UP(desc->pitch, 8U);
	}

	if ((size_t)src_stride * h > desc->buf_size) {
		LOG_ERR("buf_size %u too small for %zux%u",
			desc->buf_size, src_stride, h);
		return -EINVAL;
	}

	if (DIV_ROUND_UP(w, 2U) > cfg->line_buf_size) {
		LOG_ERR("line buffer %zu B too small for width %u",
			cfg->line_buf_size, w);
		return -ENOMEM;
	}

	ret = it8951_set_target_addr(dev, data->img_buf_addr);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_load_img_area(dev, x, y, w, h, IT8951_BPP_4, cfg->rotation);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t row = 0; row < h; row++) {
		size_t packed_len;

		if (data->pixel_format == PIXEL_FORMAT_L_8) {
			packed_len = it8951_pack_line_l8(cfg->line_buf,
							 &src[row * src_stride], w);
		} else {
			packed_len = it8951_pack_line_mono01(cfg->line_buf,
							     &src[row * src_stride], w);
		}

		ret = it8951_write_pixels(dev, cfg->line_buf, packed_len);
		if (ret < 0) {
			return ret;
		}
	}

	ret = it8951_load_img_end(dev);
	if (ret < 0) {
		return ret;
	}

	if (desc->frame_incomplete) {
		return 0;
	}

	return it8951_display_area(dev, x, y, w, h, data->waveform_mode);
}

static void it8951_get_capabilities(const struct device *dev,
				    struct display_capabilities *caps)
{
	const struct it8951_config *cfg = dev->config;
	struct it8951_data *data = dev->data;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = data->panel_w ? data->panel_w : cfg->width;
	caps->y_resolution = data->panel_h ? data->panel_h : cfg->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_L_8 | PIXEL_FORMAT_MONO01;
	caps->current_pixel_format = data->pixel_format;
	caps->current_orientation = data->orientation;
	caps->screen_info = SCREEN_INFO_EPD | SCREEN_INFO_MONO_MSB_FIRST;
}

static int it8951_blanking_on(const struct device *dev)
{
	struct it8951_data *data = dev->data;
	int ret = it8951_write_cmd(dev, IT8951_CMD_STANDBY);

	if (ret == 0) {
		data->blanking_on = true;
	}

	return ret;
}

static int it8951_blanking_off(const struct device *dev)
{
	struct it8951_data *data = dev->data;
	int ret = it8951_write_cmd(dev, IT8951_CMD_SYS_RUN);

	if (ret == 0) {
		data->blanking_on = false;
	}

	return ret;
}

static int it8951_set_pixel_format(const struct device *dev,
				   const enum display_pixel_format pf)
{
	struct it8951_data *data = dev->data;

	if (pf != PIXEL_FORMAT_L_8 && pf != PIXEL_FORMAT_MONO01) {
		return -ENOTSUP;
	}

	data->pixel_format = pf;
	if (pf == PIXEL_FORMAT_MONO01) {
		data->waveform_mode = IT8951_WAVEFORM_A2;
	}

	return 0;
}

static int it8951_set_orientation(const struct device *dev,
				  const enum display_orientation orientation)
{
	struct it8951_data *data = dev->data;

	data->orientation = orientation;
	return 0;
}

int it8951_set_waveform_mode(const struct device *dev, uint8_t mode)
{
	struct it8951_data *data = dev->data;

	if (mode > IT8951_WAVEFORM_A2) {
		return -EINVAL;
	}

	data->waveform_mode = mode;
	return 0;
}

int it8951_refresh(const struct device *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
		   uint8_t waveform_mode)
{
	struct it8951_data *data = dev->data;

	if (waveform_mode > IT8951_WAVEFORM_A2) {
		return -EINVAL;
	}

	if ((uint32_t)x + w > data->panel_w || (uint32_t)y + h > data->panel_h) {
		return -EINVAL;
	}

	return it8951_display_area(dev, x, y, w, h, waveform_mode);
}

int it8951_clear(const struct device *dev, uint8_t waveform_mode)
{
	const struct it8951_config *cfg = dev->config;
	struct it8951_data *data = dev->data;
	int ret;

	if (waveform_mode > IT8951_WAVEFORM_A2) {
		return -EINVAL;
	}

	ret = it8951_set_target_addr(dev, data->img_buf_addr);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_load_img_area(dev, 0, 0, data->panel_w, data->panel_h,
				   IT8951_BPP_4, cfg->rotation);
	if (ret < 0) {
		return ret;
	}

	memset(cfg->line_buf, 0xFF, cfg->line_buf_size);
	for (uint16_t row = 0; row < data->panel_h; row++) {
		ret = it8951_write_pixels(dev, cfg->line_buf, data->panel_w / 2U);
		if (ret < 0) {
			return ret;
		}
	}

	ret = it8951_load_img_end(dev);
	if (ret < 0) {
		return ret;
	}

	return it8951_display_area(dev, 0, 0, data->panel_w, data->panel_h, waveform_mode);
}

static int it8951_init(const struct device *dev)
{
	const struct it8951_config *cfg = dev->config;
	struct it8951_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&cfg->bus)) {
		LOG_ERR("SPI bus %s not ready", cfg->bus.bus->name);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
		LOG_ERR("reset GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&cfg->busy_gpio)) {
		LOG_ERR("busy GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&cfg->busy_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	/* Reset pulse: assert (active-low) for 100 ms, then release. */
	gpio_pin_set_dt(&cfg->reset_gpio, 1);
	k_msleep(100);
	gpio_pin_set_dt(&cfg->reset_gpio, 0);
	k_msleep(100);

	ret = it8951_wait_busy(dev);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_cmd(dev, IT8951_CMD_SYS_RUN);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_get_dev_info(dev);
	if (ret < 0) {
		return ret;
	}

	if (data->panel_w != cfg->width || data->panel_h != cfg->height) {
		LOG_WRN("DT panel %ux%u differs from controller %ux%u",
			cfg->width, cfg->height, data->panel_w, data->panel_h);
	}

	/* Enable I80 packed pixel mode for efficient bulk writes. */
	ret = it8951_write_reg(dev, IT8951_REG_I80CPCR, 0x0001U);
	if (ret < 0) {
		return ret;
	}

	if (cfg->vcom_mv != IT8951_VCOM_UNSET) {
		ret = it8951_set_vcom(dev, cfg->vcom_mv);
		if (ret < 0) {
			LOG_WRN("VCOM set failed (%d)", ret);
		}
	}

	data->pixel_format = (enum display_pixel_format)cfg->pixel_format;
	data->orientation = DISPLAY_ORIENTATION_NORMAL;
	data->waveform_mode = cfg->default_waveform;
	data->blanking_on = false;

	return 0;
}

static DEVICE_API(display, it8951_api) = {
	.blanking_on = it8951_blanking_on,
	.blanking_off = it8951_blanking_off,
	.write = it8951_write,
	.get_capabilities = it8951_get_capabilities,
	.set_pixel_format = it8951_set_pixel_format,
	.set_orientation = it8951_set_orientation,
};

#define IT8951_PIXEL_FORMAT_l8     PIXEL_FORMAT_L_8
#define IT8951_PIXEL_FORMAT_mono01 PIXEL_FORMAT_MONO01
#define IT8951_PIXEL_FORMAT(n)                                                                     \
	UTIL_CAT(IT8951_PIXEL_FORMAT_, DT_INST_STRING_TOKEN(n, pixel_format))

#define IT8951_ROTATION(n) (DT_INST_PROP(n, rotation) / 90U)

#define IT8951_LINE_BUF_BYTES(n) DIV_ROUND_UP(DT_INST_PROP(n, width), 2U)

#define IT8951_DEFINE(n)                                                                           \
	static uint8_t it8951_line_buf_##n[IT8951_LINE_BUF_BYTES(n)];                              \
	static const struct it8951_config it8951_cfg_##n = {                                       \
		.bus = SPI_DT_SPEC_INST_GET(n,                                                     \
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0),               \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(n, reset_gpios),                               \
		.busy_gpio = GPIO_DT_SPEC_INST_GET(n, busy_gpios),                                 \
		.width = DT_INST_PROP(n, width),                                                   \
		.height = DT_INST_PROP(n, height),                                                 \
		.vcom_mv = (int16_t)DT_INST_PROP_OR(n, vcom_mv, IT8951_VCOM_UNSET),                \
		.default_waveform = DT_INST_PROP(n, waveform_mode),                                \
		.pixel_format = IT8951_PIXEL_FORMAT(n),                                            \
		.rotation = IT8951_ROTATION(n),                                                    \
		.line_buf = it8951_line_buf_##n,                                                   \
		.line_buf_size = sizeof(it8951_line_buf_##n),                                      \
	};                                                                                         \
	static struct it8951_data it8951_data_##n;                                                 \
	DEVICE_DT_INST_DEFINE(n, it8951_init, NULL, &it8951_data_##n, &it8951_cfg_##n,             \
			      POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY, &it8951_api);

DT_INST_FOREACH_STATUS_OKAY(IT8951_DEFINE)
