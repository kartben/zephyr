/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT itead_it8951

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/dt-bindings/display/it8951.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(it8951, CONFIG_DISPLAY_LOG_LEVEL);

#define IT8951_SPI_PREAMBLE_WRITE_CMD	0x6000U
#define IT8951_SPI_PREAMBLE_WRITE_DATA	0x0000U
#define IT8951_SPI_PREAMBLE_READ_DATA	0x1000U

#define IT8951_TCON_SYS_RUN		0x0001U
#define IT8951_TCON_STANDBY		0x0002U
#define IT8951_TCON_SLEEP		0x0003U
#define IT8951_TCON_REG_RD		0x0010U
#define IT8951_TCON_REG_WR		0x0011U
#define IT8951_TCON_LD_IMG_AREA		0x0021U
#define IT8951_TCON_LD_IMG_END		0x0022U

#define IT8951_CMD_DISPLAY_AREA		0x0034U
#define IT8951_CMD_VCOM			0x0039U
#define IT8951_CMD_GET_DEVICE_INFO	0x0302U

#define IT8951_ROTATE_0			0U
#define IT8951_ENDIAN_LITTLE		0U
#define IT8951_PIXEL_FORMAT_8BPP	3U

#define IT8951_REG_I80CPCR		0x0004U
#define IT8951_REG_LISAR		0x0208U
#define IT8951_REG_LUTAFSR		0x1224U

#define IT8951_BUSY_DELAY_MS		1U
#define IT8951_READY_TIMEOUT_MS		1000U
#define IT8951_REFRESH_TIMEOUT_MS	45000U
#define IT8951_RESET_DELAY_MS		10U
#define IT8951_TX_CHUNK_WORDS		128U

struct it8951_dev_info {
	uint16_t panel_width;
	uint16_t panel_height;
	uint16_t image_buf_addr_low;
	uint16_t image_buf_addr_high;
	uint16_t fw_version[8];
	uint16_t lut_version[8];
};

struct it8951_config {
	struct spi_dt_spec bus;
	struct gpio_dt_spec busy_gpio;
	struct gpio_dt_spec reset_gpio;
	uint16_t width;
	uint16_t height;
	uint16_t vcom_mv;
	uint16_t update_mode;
};

struct it8951_data {
	uint32_t image_buf_addr;
	bool blanking_on;
	bool dirty;
	uint16_t dirty_x;
	uint16_t dirty_y;
	uint16_t dirty_width;
	uint16_t dirty_height;
};

static int it8951_spi_write_locked(const struct device *dev, const uint8_t *buf, size_t len)
{
	const struct it8951_config *config = dev->config;
	const struct spi_buf tx_buf = {
		.buf = (void *)buf,
		.len = len,
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1U,
	};

	return spi_write_dt(&config->bus, &tx);
}

static int it8951_spi_transceive_locked(const struct device *dev, const uint8_t *tx_buf,
					uint8_t *rx_buf, size_t len)
{
	const struct it8951_config *config = dev->config;
	const struct spi_buf txb = {
		.buf = (void *)tx_buf,
		.len = len,
	};
	const struct spi_buf rxb = {
		.buf = rx_buf,
		.len = len,
	};
	const struct spi_buf_set tx = {
		.buffers = &txb,
		.count = 1U,
	};
	const struct spi_buf_set rx = {
		.buffers = &rxb,
		.count = 1U,
	};

	return spi_transceive_dt(&config->bus, &tx, &rx);
}

static int it8951_busy_wait(const struct device *dev, uint32_t timeout_ms)
{
	const struct it8951_config *config = dev->config;
	int64_t deadline = k_uptime_get() + timeout_ms;
	int value;

	do {
		value = gpio_pin_get_dt(&config->busy_gpio);
		if (value > 0) {
			return 0;
		}

		if (value < 0) {
			return value;
		}

		k_msleep(IT8951_BUSY_DELAY_MS);
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static int it8951_write_word_locked(const struct device *dev, uint16_t word)
{
	uint8_t buf[2];

	sys_put_be16(word, buf);

	return it8951_spi_write_locked(dev, buf, sizeof(buf));
}

static int it8951_write_cmd_code(const struct device *dev, uint16_t cmd)
{
	int ret;

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_word_locked(dev, IT8951_SPI_PREAMBLE_WRITE_CMD);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_write_word_locked(dev, cmd);

out:
	spi_release_dt(&((const struct it8951_config *)dev->config)->bus);
	return ret;
}

static int it8951_write_data_word(const struct device *dev, uint16_t data)
{
	int ret;

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_word_locked(dev, IT8951_SPI_PREAMBLE_WRITE_DATA);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_write_word_locked(dev, data);

out:
	spi_release_dt(&((const struct it8951_config *)dev->config)->bus);
	return ret;
}

static int it8951_read_data_word(const struct device *dev, uint16_t *data)
{
	uint8_t zero[2] = {0};
	uint8_t rx[2];
	int ret;

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_word_locked(dev, IT8951_SPI_PREAMBLE_READ_DATA);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_spi_transceive_locked(dev, zero, rx, sizeof(zero));
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_spi_transceive_locked(dev, zero, rx, sizeof(zero));
	if (ret == 0) {
		*data = sys_get_be16(rx);
	}

out:
	spi_release_dt(&((const struct it8951_config *)dev->config)->bus);
	return ret;
}

static int it8951_read_n_data(const struct device *dev, uint16_t *data, size_t count)
{
	uint8_t tx[IT8951_TX_CHUNK_WORDS * 2] = {0};
	uint8_t rx[IT8951_TX_CHUNK_WORDS * 2];
	size_t offset = 0U;
	int ret;

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_word_locked(dev, IT8951_SPI_PREAMBLE_READ_DATA);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_spi_transceive_locked(dev, tx, rx, 2U);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	while (offset < count) {
		size_t chunk_words = MIN(count - offset, (size_t)IT8951_TX_CHUNK_WORDS);
		size_t chunk_len = chunk_words * 2U;

		memset(tx, 0, chunk_len);
		ret = it8951_spi_transceive_locked(dev, tx, rx, chunk_len);
		if (ret < 0) {
			goto out;
		}

		for (size_t i = 0; i < chunk_words; i++) {
			data[offset + i] = sys_get_be16(&rx[i * 2U]);
		}

		offset += chunk_words;
	}

out:
	spi_release_dt(&((const struct it8951_config *)dev->config)->bus);
	return ret;
}

static int it8951_send_cmd_args(const struct device *dev, uint16_t cmd, const uint16_t *args,
				size_t count)
{
	int ret = it8951_write_cmd_code(dev, cmd);

	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < count; i++) {
		ret = it8951_write_data_word(dev, args[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int it8951_read_reg(const struct device *dev, uint16_t reg, uint16_t *value)
{
	int ret = it8951_write_cmd_code(dev, IT8951_TCON_REG_RD);

	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data_word(dev, reg);
	if (ret < 0) {
		return ret;
	}

	return it8951_read_data_word(dev, value);
}

static int it8951_write_reg(const struct device *dev, uint16_t reg, uint16_t value)
{
	int ret = it8951_write_cmd_code(dev, IT8951_TCON_REG_WR);

	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data_word(dev, reg);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_data_word(dev, value);
}

static int it8951_get_device_info(const struct device *dev, struct it8951_dev_info *info)
{
	uint16_t words[sizeof(*info) / sizeof(uint16_t)];
	int ret = it8951_write_cmd_code(dev, IT8951_CMD_GET_DEVICE_INFO);

	if (ret < 0) {
		return ret;
	}

	ret = it8951_read_n_data(dev, words, ARRAY_SIZE(words));
	if (ret < 0) {
		return ret;
	}

	info->panel_width = words[0];
	info->panel_height = words[1];
	info->image_buf_addr_low = words[2];
	info->image_buf_addr_high = words[3];
	memcpy(info->fw_version, &words[4], sizeof(info->fw_version));
	memcpy(info->lut_version, &words[12], sizeof(info->lut_version));

	return 0;
}

static int it8951_set_vcom(const struct device *dev, uint16_t vcom_mv)
{
	const uint16_t args[] = { 0x0002U, vcom_mv };

	return it8951_send_cmd_args(dev, IT8951_CMD_VCOM, args, ARRAY_SIZE(args));
}

static int it8951_set_img_buf_base_addr(const struct device *dev, uint32_t image_buf_addr)
{
	int ret;

	ret = it8951_write_reg(dev, IT8951_REG_LISAR + 2U, (uint16_t)(image_buf_addr >> 16));
	if (ret < 0) {
		return ret;
	}

	return it8951_write_reg(dev, IT8951_REG_LISAR, (uint16_t)image_buf_addr);
}

static int it8951_load_img_area_start(const struct device *dev, uint16_t x, uint16_t y,
				      uint16_t width, uint16_t height)
{
	const uint16_t args[] = {
		(IT8951_ENDIAN_LITTLE << 8) |
			(IT8951_PIXEL_FORMAT_8BPP << 4) |
			IT8951_ROTATE_0,
		x,
		y,
		width,
		height,
	};

	return it8951_send_cmd_args(dev, IT8951_TCON_LD_IMG_AREA, args, ARRAY_SIZE(args));
}

static void it8951_mark_dirty(struct it8951_data *data, uint16_t x, uint16_t y, uint16_t width,
			      uint16_t height)
{
	uint16_t x2 = x + width;
	uint16_t y2 = y + height;

	if (!data->dirty) {
		data->dirty = true;
		data->dirty_x = x;
		data->dirty_y = y;
		data->dirty_width = width;
		data->dirty_height = height;
		return;
	}

	uint16_t cur_x2 = data->dirty_x + data->dirty_width;
	uint16_t cur_y2 = data->dirty_y + data->dirty_height;

	data->dirty_x = MIN(data->dirty_x, x);
	data->dirty_y = MIN(data->dirty_y, y);
	x2 = MAX(cur_x2, x2);
	y2 = MAX(cur_y2, y2);
	data->dirty_width = x2 - data->dirty_x;
	data->dirty_height = y2 - data->dirty_y;
}

static int it8951_write_pixels(const struct device *dev, uint16_t x, uint16_t y,
			       const struct display_buffer_descriptor *desc, const uint8_t *buf)
{
	struct it8951_data *data = dev->data;
	uint8_t tx[IT8951_TX_CHUNK_WORDS * 2U];
	uint16_t word_count = DIV_ROUND_UP(desc->width, 2U);
	int ret;

	ret = it8951_set_img_buf_base_addr(dev, data->image_buf_addr);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_load_img_area_start(dev, x, y, desc->width, desc->height);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_word_locked(dev, IT8951_SPI_PREAMBLE_WRITE_DATA);
	if (ret < 0) {
		goto out;
	}

	ret = it8951_busy_wait(dev, IT8951_READY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	for (uint16_t row = 0U; row < desc->height; row++) {
		const uint8_t *row_buf = buf + (row * desc->pitch);

		for (uint16_t word_base = 0U; word_base < word_count;
		     word_base += IT8951_TX_CHUNK_WORDS) {
			uint16_t chunk_words = MIN((uint16_t)(word_count - word_base),
						   (uint16_t)IT8951_TX_CHUNK_WORDS);

			for (uint16_t i = 0U; i < chunk_words; i++) {
				/* IT8951 expects host-provided 8bpp pixels in reversed
				 * 16-bit word order within each scan line.
				 */
				uint16_t src_word = word_count - 1U - (word_base + i);
				uint16_t src_idx = src_word * 2U;
				uint8_t first_pixel = (src_idx < desc->width) ? row_buf[src_idx] : 0U;
				uint8_t second_pixel =
					(src_idx + 1U < desc->width) ? row_buf[src_idx + 1U] : 0U;

				tx[i * 2U] = second_pixel;
				tx[i * 2U + 1U] = first_pixel;
			}

			ret = it8951_spi_write_locked(dev, tx, chunk_words * 2U);
			if (ret < 0) {
				goto out;
			}
		}
	}

out:
	spi_release_dt(&((const struct it8951_config *)dev->config)->bus);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_cmd_code(dev, IT8951_TCON_LD_IMG_END);
}

static int it8951_refresh(const struct device *dev, uint16_t x, uint16_t y, uint16_t width,
			  uint16_t height)
{
	const struct it8951_config *config = dev->config;
	const uint16_t args[] = {
		x,
		y,
		width,
		height,
		config->update_mode,
	};

	int ret = it8951_send_cmd_args(dev, IT8951_CMD_DISPLAY_AREA, args, ARRAY_SIZE(args));

	if (ret < 0) {
		return ret;
	}

	return it8951_busy_wait(dev, IT8951_REFRESH_TIMEOUT_MS);
}

static int it8951_wait_display_ready(const struct device *dev)
{
	int64_t deadline = k_uptime_get() + IT8951_REFRESH_TIMEOUT_MS;
	uint16_t status = 0U;
	int ret;

	do {
		ret = it8951_read_reg(dev, IT8951_REG_LUTAFSR, &status);
		if (ret < 0) {
			return ret;
		}

		if (status == 0U) {
			return 0;
		}

		k_msleep(10);
	} while (k_uptime_get() < deadline);

	return -ETIMEDOUT;
}

static int it8951_write(const struct device *dev, uint16_t x, uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	struct it8951_data *data = dev->data;
	const struct it8951_config *config = dev->config;
	int ret;

	if ((buf == NULL) || (desc->width == 0U) || (desc->height == 0U)) {
		return -EINVAL;
	}

	if ((desc->pitch < desc->width) ||
	    (desc->buf_size < (size_t)desc->pitch * desc->height) ||
	    (x + desc->width > config->width) ||
	    (y + desc->height > config->height)) {
		return -EINVAL;
	}

	ret = it8951_write_pixels(dev, x, y, desc, buf);
	if (ret < 0) {
		return ret;
	}

	it8951_mark_dirty(data, x, y, desc->width, desc->height);

	if (data->blanking_on) {
		return 0;
	}

	ret = it8951_refresh(dev, data->dirty_x, data->dirty_y, data->dirty_width,
			     data->dirty_height);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_wait_display_ready(dev);
	if (ret < 0) {
		return ret;
	}

	data->dirty = false;
	return 0;
}

static int it8951_blanking_on(const struct device *dev)
{
	struct it8951_data *data = dev->data;

	data->blanking_on = true;
	return 0;
}

static int it8951_blanking_off(const struct device *dev)
{
	struct it8951_data *data = dev->data;
	int ret;

	data->blanking_on = false;

	if (!data->dirty) {
		return 0;
	}

	ret = it8951_refresh(dev, data->dirty_x, data->dirty_y, data->dirty_width,
			     data->dirty_height);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_wait_display_ready(dev);
	if (ret < 0) {
		return ret;
	}

	data->dirty = false;
	return 0;
}

static void it8951_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct it8951_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_L_8;
	caps->current_pixel_format = PIXEL_FORMAT_L_8;
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
	caps->screen_info = SCREEN_INFO_EPD;
}

static int it8951_set_pixel_format(const struct device *dev, enum display_pixel_format pixel_format)
{
	ARG_UNUSED(dev);

	return (pixel_format == PIXEL_FORMAT_L_8) ? 0 : -ENOTSUP;
}

static int it8951_set_orientation(const struct device *dev, enum display_orientation orientation)
{
	ARG_UNUSED(dev);

	return (orientation == DISPLAY_ORIENTATION_NORMAL) ? 0 : -ENOTSUP;
}

static int it8951_hw_reset(const struct device *dev)
{
	const struct it8951_config *config = dev->config;

	if (config->reset_gpio.port == NULL) {
		return 0;
	}

	gpio_pin_set_dt(&config->reset_gpio, 1);
	k_msleep(IT8951_RESET_DELAY_MS);
	gpio_pin_set_dt(&config->reset_gpio, 0);
	k_msleep(IT8951_RESET_DELAY_MS);

	return 0;
}

static int it8951_init(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	struct it8951_dev_info dev_info;
	int ret;

	if (!spi_is_ready_dt(&config->bus)) {
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("Busy GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	if (config->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}

		it8951_hw_reset(dev);
	}

	ret = it8951_write_cmd_code(dev, IT8951_TCON_SYS_RUN);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_set_vcom(dev, config->vcom_mv);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_get_device_info(dev, &dev_info);
	if (ret < 0) {
		return ret;
	}

	if ((dev_info.panel_width != config->width) || (dev_info.panel_height != config->height)) {
		LOG_ERR("Panel size mismatch controller=%ux%u dt=%ux%u", dev_info.panel_width,
			dev_info.panel_height, config->width, config->height);
		return -EINVAL;
	}

	data->image_buf_addr = ((uint32_t)dev_info.image_buf_addr_high << 16) |
			       dev_info.image_buf_addr_low;
	data->blanking_on = true;
	data->dirty = false;

	ret = it8951_write_reg(dev, IT8951_REG_I80CPCR, 0x0001U);
	if (ret < 0) {
		return ret;
	}

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

#define IT8951_DEFINE(inst)                                                                       \
	static const struct it8951_config it8951_config_##inst = {                               \
		.bus = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |         \
						 SPI_TRANSFER_MSB | SPI_HOLD_ON_CS |    \
						 SPI_LOCK_ON),                            \
		.busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                              \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
		.vcom_mv = DT_INST_PROP(inst, vcom),                                               \
		.update_mode = DT_INST_PROP(inst, update_mode),                                    \
	};                                                                                         \
	static struct it8951_data it8951_data_##inst;                                             \
	DEVICE_DT_INST_DEFINE(inst, it8951_init, NULL, &it8951_data_##inst,                       \
			      &it8951_config_##inst, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,    \
			      &it8951_api);

DT_INST_FOREACH_STATUS_OKAY(IT8951_DEFINE)
