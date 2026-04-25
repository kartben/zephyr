/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT eink_t133a01

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(t133a01, CONFIG_DISPLAY_LOG_LEVEL);

#define T133A01_RESET_DELAY_MS      20U
#define T133A01_RESET_WAIT_MS       20U
#define T133A01_BUSY_TIMEOUT_MS     5000U
#define T133A01_REFRESH_TIMEOUT_MS  40000U
#define T133A01_POWER_STEP_DELAY_MS 30U
#define T133A01_INIT_STEP_DELAY_MS  10U
#define T133A01_STREAM_CHUNK_SIZE   64U

#define T133A01_CMD_PANEL_SETTING        0x00
#define T133A01_CMD_POWER_SETTING        0x01
#define T133A01_CMD_POWER_OFF            0x02
#define T133A01_CMD_POWER_ON             0x04
#define T133A01_CMD_BOOSTER_SOFTSTART_N  0x05
#define T133A01_CMD_BOOSTER_SOFTSTART_P  0x06
#define T133A01_CMD_DEEP_SLEEP           0x07
#define T133A01_CMD_DATA_START           0x10
#define T133A01_CMD_DISPLAY_REFRESH      0x12
#define T133A01_CMD_CDI                  0x50
#define T133A01_CMD_TCON                 0x60
#define T133A01_CMD_RESOLUTION           0x61
#define T133A01_CMD_UNKNOWN_74           0x74
#define T133A01_CMD_UNKNOWN_86           0x86
#define T133A01_CMD_DCDC                 0xA5
#define T133A01_CMD_UNKNOWN_B0           0xB0
#define T133A01_CMD_UNKNOWN_B1           0xB1
#define T133A01_CMD_UNKNOWN_B6           0xB6
#define T133A01_CMD_UNKNOWN_B7           0xB7
#define T133A01_CMD_UNKNOWN_F0           0xF0
#define T133A01_CMD_CCSET                0xE0
#define T133A01_CMD_PWS                  0xE3

#define T133A01_DEEP_SLEEP_CHECK 0xA5

enum t133a01_target {
	T133A01_TARGET_PRIMARY,
	T133A01_TARGET_SECONDARY,
	T133A01_TARGET_BOTH,
};

struct t133a01_config {
	const struct device *spi_bus;
	struct spi_config spi_config;
	struct gpio_dt_spec primary_cs_gpio;
	struct gpio_dt_spec secondary_cs_gpio;
	struct gpio_dt_spec dc_gpio;
	struct gpio_dt_spec reset_gpio;
	struct gpio_dt_spec busy_gpio;
	struct gpio_dt_spec enable_gpio;
	uint16_t width;
	uint16_t height;
	uint16_t controller_height;
};

struct t133a01_data {
	bool blanking_on;
	struct k_sem busy_sem;
	struct gpio_callback busy_cb;
};

static const uint8_t t133a01_psr[] = {0xDF, 0x69};
static const uint8_t t133a01_pwr[] = {0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38};
static const uint8_t t133a01_pof[] = {0x00};
static const uint8_t t133a01_drf[] = {0x01};
static const uint8_t t133a01_cdi[] = {0x37};
static const uint8_t t133a01_ccset_current[] = {0x01};
static const uint8_t t133a01_pws[] = {0x22};
static const uint8_t t133a01_dcdc[] = {0x44, 0x54, 0x00};
static const uint8_t t133a01_btst_p[] = {0xE0, 0x20};
static const uint8_t t133a01_btst_n[] = {0xE0, 0x20};
static const uint8_t t133a01_r74[] = {0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55};
static const uint8_t t133a01_rf0[] = {0x49, 0x55, 0x13, 0x5D, 0x05, 0x10};
static const uint8_t t133a01_r60[] = {0x03, 0x03};
static const uint8_t t133a01_r86[] = {0x10};
static const uint8_t t133a01_rb0[] = {0x01};
static const uint8_t t133a01_rb1[] = {0x02};
static const uint8_t t133a01_rb6[] = {0x07};
static const uint8_t t133a01_rb7[] = {0x01};

/*
 * The public API uses the same palette indices as the other Zephyr multi-color
 * e-paper drivers, while the T133A01 wire encoding uses controller-specific
 * nibble values for those colors.
 */
static uint8_t t133a01_map_color(uint8_t color)
{
	switch (color) {
	case 0x00:
		return 0x01;
	case 0x01:
		return 0x00;
	case 0x02:
		return 0x06;
	case 0x03:
		return 0x02;
	case 0x05:
		return 0x05;
	case 0x06:
		return 0x03;
	default:
		return 0x00;
	}
}

static int t133a01_transfer(const struct device *dev, enum t133a01_target target,
			    uint8_t dc_value, const uint8_t *buf, size_t len)
{
	const struct t133a01_config *config = dev->config;
	struct spi_buf tx_buf = {
		.buf = (void *)buf,
		.len = len,
	};
	struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};
	struct spi_config spi_config = config->spi_config;
	int ret;

	if (len == 0U) {
		return 0;
	}

	switch (target) {
	case T133A01_TARGET_PRIMARY:
	case T133A01_TARGET_BOTH:
		spi_config.cs.gpio = config->primary_cs_gpio;
		spi_config.cs.delay = 0U;
		spi_config.cs.cs_is_gpio = true;
		break;
	case T133A01_TARGET_SECONDARY:
		spi_config.cs.gpio = config->secondary_cs_gpio;
		spi_config.cs.delay = 0U;
		spi_config.cs.cs_is_gpio = true;
		ret = gpio_pin_set_dt(&config->primary_cs_gpio, 0);
		if (ret < 0) {
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	if (target != T133A01_TARGET_SECONDARY) {
		ret = gpio_pin_set_dt(&config->secondary_cs_gpio,
				      target == T133A01_TARGET_BOTH ? 1 : 0);
		if (ret < 0) {
			return ret;
		}
	}

	ret = gpio_pin_set_dt(&config->dc_gpio, dc_value);
	if (ret < 0) {
		goto out;
	}

	ret = spi_write(config->spi_bus, &spi_config, &tx);

out:
	if (target == T133A01_TARGET_BOTH) {
		int tmp = gpio_pin_set_dt(&config->secondary_cs_gpio, 0);

		if ((ret == 0) && (tmp < 0)) {
			ret = tmp;
		}
	}

	return ret;
}

static int t133a01_write_cmd(const struct device *dev, enum t133a01_target target, uint8_t cmd,
			     const uint8_t *data, size_t len)
{
	int ret;

	ret = t133a01_transfer(dev, target, 0U, &cmd, sizeof(cmd));
	if (ret < 0) {
		return ret;
	}

	return t133a01_transfer(dev, target, 1U, data, len);
}

static void t133a01_busy_cb(const struct device *gpio_dev, struct gpio_callback *cb, uint32_t pins)
{
	struct t133a01_data *data = CONTAINER_OF(cb, struct t133a01_data, busy_cb);

	ARG_UNUSED(gpio_dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->busy_sem);
}

static int t133a01_wait_until_ready(const struct device *dev, uint32_t timeout_ms)
{
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	int ret;

	ret = gpio_pin_get_dt(&config->busy_gpio);
	if (ret < 0) {
		LOG_ERR("Failed to read busy GPIO: %d", ret);
		return ret;
	}

	if (ret != 0) {
		return 0;
	}

	k_sem_reset(&data->busy_sem);

	ret = gpio_pin_interrupt_configure_dt(&config->busy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure busy GPIO interrupt: %d", ret);
		return ret;
	}

	ret = k_sem_take(&data->busy_sem, K_MSEC(timeout_ms));

	(void)gpio_pin_interrupt_configure_dt(&config->busy_gpio, GPIO_INT_DISABLE);

	if (ret < 0) {
		LOG_ERR("Timeout waiting for BUSY_N");
		return -ETIMEDOUT;
	}

	return 0;
}

static int t133a01_reset(const struct device *dev)
{
	const struct t133a01_config *config = dev->config;
	int ret;

	if (config->reset_gpio.port == NULL) {
		return 0;
	}

	ret = gpio_pin_set_dt(&config->reset_gpio, 1);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_RESET_DELAY_MS);

	ret = gpio_pin_set_dt(&config->reset_gpio, 0);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_RESET_WAIT_MS);

	return 0;
}

static int t133a01_hw_init(const struct device *dev)
{
	const struct t133a01_config *config = dev->config;
	uint8_t resolution[4] = {
		(uint8_t)(config->width >> 8),
		(uint8_t)(config->width & 0xFF),
		(uint8_t)(config->controller_height >> 8),
		(uint8_t)(config->controller_height & 0xFF),
	};
	int ret;

	ret = t133a01_reset(dev);
	if (ret < 0) {
		LOG_ERR("Failed to reset display: %d", ret);
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_UNKNOWN_74,
				t133a01_r74, ARRAY_SIZE(t133a01_r74));
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_UNKNOWN_F0,
				t133a01_rf0, ARRAY_SIZE(t133a01_rf0));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_PANEL_SETTING,
				t133a01_psr, ARRAY_SIZE(t133a01_psr));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_DCDC,
				t133a01_dcdc, ARRAY_SIZE(t133a01_dcdc));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_CDI,
				t133a01_cdi, ARRAY_SIZE(t133a01_cdi));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_TCON,
				t133a01_r60, ARRAY_SIZE(t133a01_r60));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_UNKNOWN_86,
				t133a01_r86, ARRAY_SIZE(t133a01_r86));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_PWS,
				t133a01_pws, ARRAY_SIZE(t133a01_pws));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_RESOLUTION,
				resolution, ARRAY_SIZE(resolution));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_POWER_SETTING,
				t133a01_pwr, ARRAY_SIZE(t133a01_pwr));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_UNKNOWN_B6,
				t133a01_rb6, ARRAY_SIZE(t133a01_rb6));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY,
				T133A01_CMD_BOOSTER_SOFTSTART_P, t133a01_btst_p,
				ARRAY_SIZE(t133a01_btst_p));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_UNKNOWN_B7,
				t133a01_rb7, ARRAY_SIZE(t133a01_rb7));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY,
				T133A01_CMD_BOOSTER_SOFTSTART_N, t133a01_btst_n,
				ARRAY_SIZE(t133a01_btst_n));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_UNKNOWN_B0,
				t133a01_rb0, ARRAY_SIZE(t133a01_rb0));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_UNKNOWN_B1,
				t133a01_rb1, ARRAY_SIZE(t133a01_rb1));
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	return 0;
}

static int t133a01_deep_sleep(const struct device *dev)
{
	const uint8_t deep_sleep = T133A01_DEEP_SLEEP_CHECK;
	int ret;

	ret = t133a01_write_cmd(dev, T133A01_TARGET_PRIMARY, T133A01_CMD_DEEP_SLEEP,
				&deep_sleep, sizeof(deep_sleep));
	if (ret < 0) {
		return ret;
	}

	k_msleep(1);

	return t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
}

static int t133a01_write_half(const struct device *dev, enum t133a01_target target,
			      const uint8_t *buf, size_t row_bytes, uint16_t height)
{
	uint8_t converted[T133A01_STREAM_CHUNK_SIZE];
	size_t row_offset = 0U;
	int ret;

	ret = t133a01_write_cmd(dev, target, T133A01_CMD_DATA_START, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t row = 0U; row < height; row++) {
		size_t col = 0U;

		while (col < row_bytes) {
			size_t chunk_len = MIN((size_t)T133A01_STREAM_CHUNK_SIZE, row_bytes - col);

			for (size_t i = 0U; i < chunk_len; i++) {
				uint8_t pixel_pair = buf[row_offset + col + i];
				uint8_t first = t133a01_map_color((pixel_pair >> 4) & 0x0F);
				uint8_t second = t133a01_map_color(pixel_pair & 0x0F);

				converted[i] = (uint8_t)((first << 4) | second);
			}

			ret = t133a01_transfer(dev, target, 1U, converted, chunk_len);
			if (ret < 0) {
				return ret;
			}

			col += chunk_len;
		}

		/* Advance to the next full source row, then keep the same controller half. */
		row_offset += row_bytes * 2U;
	}

	return 0;
}

static int t133a01_update_display(const struct device *dev)
{
	int ret;

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_POWER_ON, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_DISPLAY_REFRESH,
				t133a01_drf, ARRAY_SIZE(t133a01_drf));
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_REFRESH_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_STEP_DELAY_MS);

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_POWER_OFF,
				t133a01_pof, ARRAY_SIZE(t133a01_pof));
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_STEP_DELAY_MS);

	return 0;
}

static int t133a01_write(const struct device *dev, uint16_t x, uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	const uint8_t *bytes = buf;
	size_t buf_len = (size_t)config->width * config->height / 2U;
	size_t row_bytes;
	int ret;

	if ((x != 0U) || (y != 0U) || (desc->width != config->width) ||
	    (desc->height != config->height)) {
		LOG_ERR("Partial updates not supported");
		return -ENOTSUP;
	}

	if (desc->pitch != desc->width) {
		LOG_ERR("Pitch must match width");
		return -EINVAL;
	}

	if ((buf == NULL) || (desc->buf_size < buf_len)) {
		LOG_ERR("Invalid buffer: %p (%zu < %zu)", buf, desc->buf_size, buf_len);
		return -EINVAL;
	}

	ret = t133a01_hw_init(dev);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_write_cmd(dev, T133A01_TARGET_BOTH, T133A01_CMD_CCSET,
				t133a01_ccset_current,
				ARRAY_SIZE(t133a01_ccset_current));
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_INIT_STEP_DELAY_MS);

	/* 4-bit pixels are packed two per byte, and each controller gets half a row. */
	row_bytes = config->width / 4U;

	ret = t133a01_write_half(dev, T133A01_TARGET_PRIMARY, bytes, row_bytes, config->height);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_write_half(dev, T133A01_TARGET_SECONDARY, bytes + row_bytes,
				 row_bytes, config->height);
	if (ret < 0) {
		return ret;
	}

	if (!data->blanking_on) {
		ret = t133a01_update_display(dev);
		if (ret < 0) {
			return ret;
		}

		ret = t133a01_deep_sleep(dev);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int t133a01_blanking_on(const struct device *dev)
{
	struct t133a01_data *data = dev->data;

	data->blanking_on = true;

	return 0;
}

static int t133a01_blanking_off(const struct device *dev)
{
	struct t133a01_data *data = dev->data;

	data->blanking_on = false;

	return 0;
}

static void t133a01_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct t133a01_config *config = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_I_4;
	caps->current_pixel_format = PIXEL_FORMAT_I_4;
	caps->screen_info = SCREEN_INFO_EPD;
}

static int t133a01_set_pixel_format(const struct device *dev, enum display_pixel_format pf)
{
	ARG_UNUSED(dev);

	if (pf == PIXEL_FORMAT_I_4) {
		return 0;
	}

	return -ENOTSUP;
}

static int t133a01_init(const struct device *dev)
{
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->spi_bus)) {
		LOG_ERR_DEVICE_NOT_READY(config->spi_bus);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->primary_cs_gpio)) {
		LOG_ERR("Primary CS GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->secondary_cs_gpio)) {
		LOG_ERR("Secondary CS GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->dc_gpio)) {
		LOG_ERR("DC GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("Busy GPIO not ready");
		return -ENODEV;
	}

	if ((config->reset_gpio.port != NULL) && !gpio_is_ready_dt(&config->reset_gpio)) {
		LOG_ERR("Reset GPIO not ready");
		return -ENODEV;
	}

	if ((config->enable_gpio.port != NULL) && !gpio_is_ready_dt(&config->enable_gpio)) {
		LOG_ERR("Enable GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->primary_cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&config->secondary_cs_gpio, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&config->dc_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	if (config->reset_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	if (config->enable_gpio.port != NULL) {
		ret = gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	k_sem_init(&data->busy_sem, 0, 1);
	gpio_init_callback(&data->busy_cb, t133a01_busy_cb, BIT(config->busy_gpio.pin));

	ret = gpio_add_callback(config->busy_gpio.port, &data->busy_cb);
	if (ret < 0) {
		return ret;
	}

	data->blanking_on = true;

	return t133a01_hw_init(dev);
}

#ifdef CONFIG_PM_DEVICE
static int t133a01_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return t133a01_hw_init(dev);
	case PM_DEVICE_ACTION_SUSPEND:
		return t133a01_deep_sleep(dev);
	default:
		return -ENOTSUP;
	}
}
#endif

static DEVICE_API(display, t133a01_api) = {
	.blanking_on = t133a01_blanking_on,
	.blanking_off = t133a01_blanking_off,
	.write = t133a01_write,
	.get_capabilities = t133a01_get_capabilities,
	.set_pixel_format = t133a01_set_pixel_format,
};

#define T133A01_PRIMARY_CS_SPEC(inst)                                                       \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, primary_cs_gpios),                          \
		    (GPIO_DT_SPEC_INST_GET(inst, primary_cs_gpios)),                         \
		    (MIPI_DBI_SPI_CS_GPIOS_DT_SPEC_GET(DT_DRV_INST(inst))))

#define T133A01_DEFINE(inst)                                                                 \
	BUILD_ASSERT((DT_INST_PROP(inst, width) % 4) == 0,                                \
		     "T133A01 width must be a multiple of 4");                          \
	BUILD_ASSERT((DT_INST_PROP(inst, height) % 2) == 0, "T133A01 height must be even");  \
	static const struct t133a01_config t133a01_cfg_##inst = {                          \
		.spi_bus = DEVICE_DT_GET(MIPI_DBI_DT_SPI_DEV(DT_DRV_INST(inst))),          \
		.spi_config =                                                               \
			MIPI_DBI_SPI_CONFIG_DT_INST(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), \
						    0),                                         \
		.primary_cs_gpio = T133A01_PRIMARY_CS_SPEC(inst),                           \
		.secondary_cs_gpio = GPIO_DT_SPEC_INST_GET(inst, secondary_cs_gpios),       \
		.dc_gpio = GPIO_DT_SPEC_GET(DT_PARENT(DT_DRV_INST(inst)), dc_gpios),        \
		.reset_gpio = GPIO_DT_SPEC_GET_OR(DT_PARENT(DT_DRV_INST(inst)), reset_gpios, {}), \
		.busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                       \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {}),            \
		.width = DT_INST_PROP(inst, width),                                         \
		.height = DT_INST_PROP(inst, height),                                       \
		.controller_height = DT_INST_PROP(inst, height) / 2U,                       \
	};                                                                                 \
	static struct t133a01_data t133a01_data_##inst;                                    \
	PM_DEVICE_DT_INST_DEFINE(inst, t133a01_pm_action);                                 \
	DEVICE_DT_INST_DEFINE(inst, t133a01_init, PM_DEVICE_DT_INST_GET(inst),             \
			      &t133a01_data_##inst, &t133a01_cfg_##inst, POST_KERNEL,       \
			      CONFIG_DISPLAY_INIT_PRIORITY, &t133a01_api);

DT_INST_FOREACH_STATUS_OKAY(T133A01_DEFINE)
