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
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>

LOG_MODULE_REGISTER(it8951, CONFIG_DISPLAY_LOG_LEVEL);

#define IT8951_PREAMBLE_WRITE_CMD  0x6000U
#define IT8951_PREAMBLE_WRITE_DATA 0x0000U
#define IT8951_PREAMBLE_READ_DATA  0x1000U

#define IT8951_CMD_SYS_RUN      0x0001U
#define IT8951_CMD_STANDBY      0x0002U
#define IT8951_CMD_SLEEP        0x0003U
#define IT8951_CMD_REG_READ     0x0010U
#define IT8951_CMD_REG_WRITE    0x0011U
#define IT8951_CMD_LOAD_IMAGE   0x0020U
#define IT8951_CMD_LOAD_AREA    0x0021U
#define IT8951_CMD_LOAD_END     0x0022U
#define IT8951_CMD_DISPLAY_AREA 0x0034U
#define IT8951_CMD_VCOM         0x0039U
#define IT8951_CMD_GET_INFO     0x0302U

#define IT8951_REG_BASE      0x1000U
#define IT8951_REG_I80CPCR   0x0004U
#define IT8951_REG_LISAR     0x0208U
#define IT8951_REG_LUTAFSR   (IT8951_REG_BASE + 0x0224U)
#define IT8951_PACKED_ENABLE 0x0001U

#define IT8951_ENDIAN_LITTLE 0U
#define IT8951_ROTATE_0      0U
#define IT8951_4BPP          2U

struct it8951_dev_info {
	uint16_t panel_width;
	uint16_t panel_height;
	uint16_t img_buf_addr_l;
	uint16_t img_buf_addr_h;
	uint16_t fw_version[8];
	uint16_t lut_version[8];
};

struct it8951_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec busy_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec enable_gpio;
	struct gpio_dt_spec ite_enable_gpio;
	uint16_t width;
	uint16_t height;
	uint16_t vcom_mv;
	uint16_t refresh_mode;
	uint16_t pixel_format;
	uint16_t reset_delay_ms;
	uint16_t reset_wait_ms;
	uint32_t ready_timeout_ms;
	uint32_t refresh_timeout_ms;
};

struct it8951_data {
	uint32_t img_buf_addr;
	bool blanking_on;
};

static int it8951_wait_ready(const struct device *dev, uint32_t timeout_ms)
{
	const struct it8951_config *config = dev->config;
	int64_t end = k_uptime_get() + timeout_ms;
	int pin;

	do {
		pin = gpio_pin_get_dt(&config->busy_gpio);
		if (pin < 0) {
			LOG_ERR("Failed to read busy GPIO: %d", pin);
			return pin;
		}

		if (pin != 0) {
			return 0;
		}

		k_msleep(1);
	} while (k_uptime_get() < end);

	LOG_ERR("Timeout waiting for HRDY");
	return -ETIMEDOUT;
}

static int it8951_spi_transceive(const struct device *dev, const uint8_t *tx_buf, uint8_t *rx_buf,
				 size_t len)
{
	const struct it8951_config *config = dev->config;
	const struct spi_buf tx = {
		.buf = (void *)tx_buf,
		.len = len,
	};
	const struct spi_buf rx = {
		.buf = rx_buf,
		.len = len,
	};
	const struct spi_buf_set tx_set = {
		.buffers = &tx,
		.count = 1,
	};
	const struct spi_buf_set rx_set = {
		.buffers = &rx,
		.count = 1,
	};

	if (rx_buf == NULL) {
		return spi_write_dt(&config->spi, &tx_set);
	}

	return spi_transceive_dt(&config->spi, &tx_set, &rx_set);
}

static int it8951_transfer_word(const struct device *dev, uint16_t tx_word, uint16_t *rx_word)
{
	uint8_t tx_buf[2];
	uint8_t rx_buf[2];
	int ret;

	sys_put_be16(tx_word, tx_buf);

	ret = it8951_spi_transceive(dev, tx_buf, rx_word == NULL ? NULL : rx_buf, sizeof(tx_buf));
	if (ret < 0) {
		return ret;
	}

	if (rx_word != NULL) {
		*rx_word = sys_get_be16(rx_buf);
	}

	return 0;
}

static int it8951_write_preamble_word(const struct device *dev, uint16_t preamble, uint16_t word)
{
	const struct it8951_config *config = dev->config;
	int ret;

	ret = it8951_wait_ready(dev, config->ready_timeout_ms);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_transfer_word(dev, preamble, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_wait_ready(dev, config->ready_timeout_ms);
	if (ret < 0) {
		return ret;
	}

	return it8951_transfer_word(dev, word, NULL);
}

static int it8951_write_cmd(const struct device *dev, uint16_t cmd)
{
	return it8951_write_preamble_word(dev, IT8951_PREAMBLE_WRITE_CMD, cmd);
}

static int it8951_write_data(const struct device *dev, uint16_t data)
{
	return it8951_write_preamble_word(dev, IT8951_PREAMBLE_WRITE_DATA, data);
}

static int it8951_read_data(const struct device *dev, uint16_t *data)
{
	const struct it8951_config *config = dev->config;
	int ret;

	ret = it8951_wait_ready(dev, config->ready_timeout_ms);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_transfer_word(dev, IT8951_PREAMBLE_READ_DATA, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_transfer_word(dev, 0x0000U, NULL);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_wait_ready(dev, config->ready_timeout_ms);
	if (ret < 0) {
		return ret;
	}

	return it8951_transfer_word(dev, 0x0000U, data);
}

static int it8951_send_cmd_args(const struct device *dev, uint16_t cmd, const uint16_t *args,
				size_t arg_count)
{
	int ret;

	ret = it8951_write_cmd(dev, cmd);
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < arg_count; i++) {
		ret = it8951_write_data(dev, args[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int it8951_read_reg(const struct device *dev, uint16_t reg, uint16_t *value)
{
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_REG_READ);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, reg);
	if (ret < 0) {
		return ret;
	}

	return it8951_read_data(dev, value);
}

static int it8951_write_reg(const struct device *dev, uint16_t reg, uint16_t value)
{
	const uint16_t args[] = {reg, value};

	return it8951_send_cmd_args(dev, IT8951_CMD_REG_WRITE, args, ARRAY_SIZE(args));
}

static int it8951_get_info(const struct device *dev, struct it8951_dev_info *info)
{
	uint16_t *words = (uint16_t *)info;
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_GET_INFO);
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0; i < sizeof(*info) / sizeof(uint16_t); i++) {
		ret = it8951_read_data(dev, &words[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int it8951_set_img_buf_base_addr(const struct device *dev, uint32_t addr)
{
	int ret;

	ret = it8951_write_reg(dev, IT8951_REG_LISAR + 2U, (uint16_t)(addr >> 16));
	if (ret < 0) {
		return ret;
	}

	return it8951_write_reg(dev, IT8951_REG_LISAR, (uint16_t)addr);
}

static int it8951_set_vcom(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	const uint16_t args[] = {0x0002U, config->vcom_mv};

	return it8951_send_cmd_args(dev, IT8951_CMD_VCOM, args, ARRAY_SIZE(args));
}

static uint16_t it8951_reverse_bits_16(uint16_t value)
{
	value = ((value & 0xAAAAU) >> 1) | ((value & 0x5555U) << 1);
	value = ((value & 0xCCCCU) >> 2) | ((value & 0x3333U) << 2);
	value = ((value & 0xF0F0U) >> 4) | ((value & 0x0F0FU) << 4);
	value = ((value & 0xFF00U) >> 8) | ((value & 0x00FFU) << 8);

	return value;
}

static int it8951_write_packed_l4(const struct device *dev, const uint8_t *buf, uint16_t x,
				  uint16_t y, uint16_t width, uint16_t height, bool flip_bits)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	uint16_t load_arg = (IT8951_ENDIAN_LITTLE << 8) | (config->pixel_format << 4) |
			    IT8951_ROTATE_0;
	uint16_t args[] = {load_arg, x, y, width, height};
	uint16_t words_per_line = DIV_ROUND_UP(width, 4U);
	int ret;

	ret = it8951_set_img_buf_base_addr(dev, data->img_buf_addr);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_send_cmd_args(dev, IT8951_CMD_LOAD_AREA, args, ARRAY_SIZE(args));
	if (ret < 0) {
		return ret;
	}

	ret = it8951_wait_ready(dev, config->ready_timeout_ms);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_transfer_word(dev, IT8951_PREAMBLE_WRITE_DATA, NULL);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t row = 0; row < height; row++) {
		for (uint16_t word = 0; word < words_per_line; word++) {
			uint16_t src_word = words_per_line - 1U - word;
			size_t offset = ((size_t)row * words_per_line + src_word) * sizeof(uint16_t);
			uint16_t value;

			memcpy(&value, buf + offset, sizeof(value));
			if (flip_bits) {
				value = it8951_reverse_bits_16(value);
			}

			ret = it8951_transfer_word(dev, value, NULL);
			if (ret < 0) {
				return ret;
			}
		}
	}

	return it8951_write_cmd(dev, IT8951_CMD_LOAD_END);
}

static int it8951_display_area(const struct device *dev, uint16_t x, uint16_t y, uint16_t width,
			       uint16_t height)
{
	const struct it8951_config *config = dev->config;
	const uint16_t args[] = {x, y, width, height, config->refresh_mode};

	return it8951_send_cmd_args(dev, IT8951_CMD_DISPLAY_AREA, args, ARRAY_SIZE(args));
}

static int it8951_wait_display_ready(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	int64_t end = k_uptime_get() + config->refresh_timeout_ms;
	uint16_t status;
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
	} while (k_uptime_get() < end);

	LOG_ERR("Timeout waiting for display refresh");
	return -ETIMEDOUT;
}

static int it8951_hw_reset(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	int ret;

	if (config->enable_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&config->enable_gpio, 1);
		if (ret < 0) {
			return ret;
		}
	}

	if (config->ite_enable_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&config->ite_enable_gpio, 1);
		if (ret < 0) {
			return ret;
		}
	}

	ret = gpio_pin_set_dt(&config->reset_gpio, 0);
	if (ret < 0) {
		return ret;
	}

	k_msleep(config->reset_delay_ms);

	ret = gpio_pin_set_dt(&config->reset_gpio, 1);
	if (ret < 0) {
		return ret;
	}

	k_msleep(config->reset_wait_ms);

	return it8951_wait_ready(dev, config->ready_timeout_ms);
}

static int it8951_controller_init(const struct device *dev, bool full_init)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	struct it8951_dev_info info;
	int ret;

	if (full_init) {
		ret = it8951_set_vcom(dev);
		if (ret < 0) {
			LOG_ERR("Failed to set VCOM: %d", ret);
			return ret;
		}
	}

	ret = it8951_get_info(dev, &info);
	if (ret < 0) {
		LOG_ERR("Failed to read device info: %d", ret);
		return ret;
	}

	if (info.panel_width == 0U || info.panel_height == 0U) {
		LOG_ERR("Invalid panel size from IT8951");
		return -ENODEV;
	}

	if (info.panel_width != config->width || info.panel_height != config->height) {
		LOG_WRN("IT8951 reports %ux%u panel, devicetree config is %ux%u",
			info.panel_width, info.panel_height, config->width, config->height);
	}

	data->img_buf_addr = info.img_buf_addr_l | ((uint32_t)info.img_buf_addr_h << 16);

	if (full_init) {
		ret = it8951_write_reg(dev, IT8951_REG_I80CPCR, IT8951_PACKED_ENABLE);
		if (ret < 0) {
			LOG_ERR("Failed to enable packed mode: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int it8951_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	uint16_t controller_x;
	size_t line_bytes;
	size_t buf_len;
	int ret;

	if (buf == NULL) {
		return -EINVAL;
	}

	if (desc->pitch != desc->width) {
		LOG_ERR("Pitch must match width");
		return -EINVAL;
	}

	if ((desc->width % 4U) != 0U) {
		LOG_ERR("Write width must be a multiple of 4 pixels");
		return -EINVAL;
	}

	if ((x + desc->width) > config->width || (y + desc->height) > config->height) {
		LOG_ERR("Write area outside panel bounds");
		return -EINVAL;
	}

	line_bytes = DIV_ROUND_UP(desc->width, 2U);
	buf_len = line_bytes * desc->height;
	if (desc->buf_size < buf_len) {
		LOG_ERR("Invalid buffer size: %zu < %zu", desc->buf_size, buf_len);
		return -EINVAL;
	}

	controller_x = config->width - x - desc->width;

	ret = it8951_controller_init(dev, false);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_packed_l4(dev, buf, controller_x, y, desc->width, desc->height, false);
	if (ret < 0) {
		return ret;
	}

	if (!data->blanking_on) {
		ret = it8951_display_area(dev, controller_x, y, desc->width, desc->height);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_wait_display_ready(dev);
	}

	return ret;
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

	data->blanking_on = false;
	return 0;
}

static void it8951_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct it8951_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_I_4;
	caps->current_pixel_format = PIXEL_FORMAT_I_4;
	caps->screen_info = SCREEN_INFO_EPD;
}

static int it8951_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	ARG_UNUSED(dev);

	if (pf == PIXEL_FORMAT_I_4) {
		return 0;
	}

	return -ENOTSUP;
}

static int it8951_init(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	int ret;

	if (!spi_is_ready_dt(&config->spi)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("Busy GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->reset_gpio)) {
		LOG_ERR("Reset GPIO not ready");
		return -ENODEV;
	}

	if (config->enable_gpio.port != NULL && !gpio_is_ready_dt(&config->enable_gpio)) {
		LOG_ERR("Enable GPIO not ready");
		return -ENODEV;
	}

	if (config->ite_enable_gpio.port != NULL && !gpio_is_ready_dt(&config->ite_enable_gpio)) {
		LOG_ERR("ITE enable GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure busy GPIO: %d", ret);
		return ret;
	}

	ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure reset GPIO: %d", ret);
		return ret;
	}

	if (config->enable_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO: %d", ret);
			return ret;
		}
	}

	if (config->ite_enable_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->ite_enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure ITE enable GPIO: %d", ret);
			return ret;
		}
	}

	data->blanking_on = true;

	ret = it8951_hw_reset(dev);
	if (ret < 0) {
		return ret;
	}

	return it8951_controller_init(dev, true);
}

#ifdef CONFIG_PM_DEVICE
static int it8951_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		ret = it8951_hw_reset(dev);
		if (ret < 0) {
			return ret;
		}
		return it8951_controller_init(dev, false);
	case PM_DEVICE_ACTION_SUSPEND:
		return it8951_write_cmd(dev, IT8951_CMD_SLEEP);
	default:
		return -ENOTSUP;
	}
}
#endif

static DEVICE_API(display, it8951_api) = {
	.blanking_on = it8951_blanking_on,
	.blanking_off = it8951_blanking_off,
	.write = it8951_write,
	.get_capabilities = it8951_get_capabilities,
	.set_pixel_format = it8951_set_pixel_format,
};

#define IT8951_GPIO_DT_SPEC_INST_GET_OR(inst, prop)                                                \
	GPIO_DT_SPEC_GET_OR(DT_DRV_INST(inst), prop, {0})

#define IT8951_DEFINE(inst)                                                                        \
	static const struct it8951_config it8951_cfg_##inst = {                                    \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8U)),          \
		.busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                              \
		.reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                            \
		.enable_gpio = IT8951_GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios),                \
		.ite_enable_gpio = IT8951_GPIO_DT_SPEC_INST_GET_OR(inst, ite_enable_gpios),        \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
		.vcom_mv = DT_INST_PROP(inst, vcom_mv),                                            \
		.refresh_mode = DT_INST_PROP(inst, refresh_mode),                                  \
		.pixel_format = DT_INST_PROP(inst, pixel_format),                                  \
		.reset_delay_ms = DT_INST_PROP(inst, reset_delay_ms),                              \
		.reset_wait_ms = DT_INST_PROP(inst, reset_wait_ms),                                \
		.ready_timeout_ms = DT_INST_PROP(inst, ready_timeout_ms),                          \
		.refresh_timeout_ms = DT_INST_PROP(inst, refresh_timeout_ms),                      \
	};                                                                                         \
	static struct it8951_data it8951_data_##inst;                                               \
	PM_DEVICE_DT_INST_DEFINE(inst, it8951_pm_action);                                           \
	DEVICE_DT_INST_DEFINE(inst, it8951_init, PM_DEVICE_DT_INST_GET(inst),                       \
			      &it8951_data_##inst, &it8951_cfg_##inst, POST_KERNEL,                \
			      CONFIG_DISPLAY_INIT_PRIORITY, &it8951_api);

DT_INST_FOREACH_STATUS_OKAY(IT8951_DEFINE)
