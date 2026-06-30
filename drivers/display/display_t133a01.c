/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT eink_t133a01

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/color_dither.h>
#include <zephyr/drivers/display/t133a01.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(t133a01, CONFIG_DISPLAY_LOG_LEVEL);

#define T133A01_RESET_DELAY_MS    20U
#define T133A01_RESET_WAIT_MS     20U
#define T133A01_INIT_CMD_DELAY_MS 10U
#define T133A01_POWER_DELAY_MS    30U
#define T133A01_BUSY_TIMEOUT_MS   5000U  /* 5 seconds for normal operations */
#define T133A01_REFRESH_TIMEOUTMS 40000U /* 40 seconds for display refresh */

#define T133A01_CMD_PSR   0x00
#define T133A01_CMD_PWR   0x01
#define T133A01_CMD_POF   0x02
#define T133A01_CMD_PON   0x04
#define T133A01_CMD_BTSTN 0x05
#define T133A01_CMD_BTSTP 0x06
#define T133A01_CMD_DSLP  0x07
#define T133A01_CMD_DTM   0x10
#define T133A01_CMD_DRF   0x12
#define T133A01_CMD_CDI   0x50
#define T133A01_CMD_TRES  0x61
#define T133A01_CMD_DCDC  0xA5
#define T133A01_CMD_CCSET 0xE0
#define T133A01_CMD_PWS   0xE3

#define T133A01_DEEP_SLEEP_CHECK 0xA5

/* Target of an init-sequence command: the master die only, or both dies. */
#define T133A01_TGT_MASTER 0U
#define T133A01_TGT_BOTH   1U

struct t133a01_config {
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_master;
	struct gpio_dt_spec slave_cs;
	struct gpio_dt_spec busy_gpio;
	struct gpio_dt_spec enable_gpio;
	uint32_t color_palette[T133A01_COLOR_PALETTE_SIZE];
	uint16_t width;
	uint16_t height;
};

struct t133a01_data {
	struct display_color_dither_state color_dither;
	/* Copy of dbi_master with the chip-select overridden to the slave die. */
	struct mipi_dbi_config dbi_slave;
	bool blanking_on;
	struct k_sem busy_sem;
	struct gpio_callback busy_cb;
};

/* Send a command (with optional data) to the master die only. */
static int t133a01_cmd_master(const struct device *dev, uint8_t cmd, const uint8_t *data,
			      size_t len)
{
	const struct t133a01_config *config = dev->config;

	return mipi_dbi_command_write(config->mipi_dev, &config->dbi_master, cmd, data, len);
}

/*
 * Send a command (with optional data) to both dies at once. The master die is
 * selected by the MIPI DBI transfer; the slave die is selected in parallel by
 * holding its chip-select low for the duration of the transfer.
 */
static int t133a01_cmd_both(const struct device *dev, uint8_t cmd, const uint8_t *data, size_t len)
{
	const struct t133a01_config *config = dev->config;
	int ret;

	gpio_pin_set_dt(&config->slave_cs, 1);
	ret = mipi_dbi_command_write(config->mipi_dev, &config->dbi_master, cmd, data, len);
	gpio_pin_set_dt(&config->slave_cs, 0);

	return ret;
}

/* Run a packed init sequence of {target, cmd, len, bytes...} tuples. */
static int t133a01_run_init_sequence(const struct device *dev, const uint8_t *seq, size_t seq_len)
{
	size_t i = 0;

	while (i < seq_len) {
		uint8_t target = seq[i++];
		uint8_t cmd = seq[i++];
		uint8_t len = seq[i++];
		const uint8_t *data = len ? &seq[i] : NULL;
		int ret;

		if (target == T133A01_TGT_BOTH) {
			ret = t133a01_cmd_both(dev, cmd, data, len);
		} else {
			ret = t133a01_cmd_master(dev, cmd, data, len);
		}

		if (ret < 0) {
			return ret;
		}

		i += len;
		k_msleep(T133A01_INIT_CMD_DELAY_MS);
	}

	return 0;
}

static void t133a01_busy_cb(const struct device *gpio_dev, struct gpio_callback *cb, uint32_t pins)
{
	struct t133a01_data *data = CONTAINER_OF(cb, struct t133a01_data, busy_cb);

	ARG_UNUSED(gpio_dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->busy_sem);
}

/* Wait until BUSY_N indicates the panel is idle. */
static int t133a01_wait_until_ready(const struct device *dev, uint32_t timeout_ms)
{
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	int pin;
	int ret;

	pin = gpio_pin_get_dt(&config->busy_gpio);
	if (pin < 0) {
		LOG_ERR("Failed to read busy GPIO: %d", pin);
		return pin;
	}

	if (pin != 0) {
		return 0;
	}

	k_sem_reset(&data->busy_sem);

	ret = gpio_pin_interrupt_configure_dt(&config->busy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure busy GPIO interrupt: %d", ret);
		return ret;
	}

	ret = k_sem_take(&data->busy_sem, K_MSEC(timeout_ms));

	gpio_pin_interrupt_configure_dt(&config->busy_gpio, GPIO_INT_DISABLE);

	if (ret < 0) {
		LOG_ERR("Timeout waiting for BUSY_N");
		return -ETIMEDOUT;
	}

	return 0;
}

static int t133a01_hw_init(const struct device *dev)
{
	const struct t133a01_config *config = dev->config;
	const uint8_t init_seq[] = {
		T133A01_TGT_MASTER, 0x74, 9, 0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15, 0x55,
		T133A01_TGT_BOTH,   0xF0, 6, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10,
		T133A01_TGT_BOTH,   T133A01_CMD_PSR, 2, 0xDF, 0x69,
		T133A01_TGT_MASTER, T133A01_CMD_DCDC, 3, 0x44, 0x54, 0x00,
		T133A01_TGT_BOTH,   T133A01_CMD_CDI, 1, 0x37,
		T133A01_TGT_BOTH,   0x60, 2, 0x03, 0x03,
		T133A01_TGT_BOTH,   0x86, 1, 0x10,
		T133A01_TGT_BOTH,   T133A01_CMD_PWS, 1, 0x22,
		/* Per-die resolution: panel width x (panel height / 2). */
		T133A01_TGT_BOTH,   T133A01_CMD_TRES, 4, (config->width >> 8) & 0xFF,
							 config->width & 0xFF,
							 ((config->height / 2) >> 8) & 0xFF,
				 			 (config->height / 2) & 0xFF,
		T133A01_TGT_MASTER, T133A01_CMD_PWR, 6, 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38,
		T133A01_TGT_MASTER, 0xB6, 1, 0x07,
		T133A01_TGT_MASTER, T133A01_CMD_BTSTP, 2, 0xE0, 0x20,
		T133A01_TGT_MASTER, 0xB7, 1, 0x01,
		T133A01_TGT_MASTER, T133A01_CMD_BTSTN, 2, 0xE0, 0x20,
		T133A01_TGT_MASTER, 0xB0, 1, 0x01,
		T133A01_TGT_MASTER, 0xB1, 1, 0x02,
	};
	int ret;

	ret = mipi_dbi_reset(config->mipi_dev, T133A01_RESET_DELAY_MS);
	if (ret < 0) {
		LOG_ERR("Failed to reset display: %d", ret);
		return ret;
	}

	k_msleep(T133A01_RESET_WAIT_MS);

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	ret = t133a01_run_init_sequence(dev, init_seq, sizeof(init_seq));

out:
	mipi_dbi_release(config->mipi_dev, &config->dbi_master);
	return ret;
}

static int t133a01_deep_sleep(const struct device *dev)
{
	/* The reference firmware puts only the master die into deep sleep. */
	uint8_t check = T133A01_DEEP_SLEEP_CHECK;

	return t133a01_cmd_master(dev, T133A01_CMD_DSLP, &check, 1);
}

/* Stream one half-panel worth of pixel data to a single die. */
static int t133a01_push_half(const struct device *dev, const struct mipi_dbi_config *dbi,
			     const uint8_t *src, size_t stride, size_t half_bytes)
{
	const struct t133a01_config *config = dev->config;
	struct display_buffer_descriptor desc = {
		.height = 1,
		.width = half_bytes,
		.pitch = half_bytes,
		.buf_size = half_bytes,
	};
	int ret;

	ret = mipi_dbi_command_write(config->mipi_dev, dbi, T133A01_CMD_DTM, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t row = 0; row < config->height; row++) {
		desc.frame_incomplete = (row != config->height - 1);

		ret = mipi_dbi_write_display(config->mipi_dev, dbi, src + (size_t)row * stride,
					     &desc, PIXEL_FORMAT_I_4);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int t133a01_refresh(const struct device *dev)
{
	uint8_t drf = 0x01;
	uint8_t pof = 0x00;
	int ret;

	ret = t133a01_cmd_both(dev, T133A01_CMD_PON, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	ret = t133a01_cmd_both(dev, T133A01_CMD_DRF, &drf, 1);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_REFRESH_TIMEOUTMS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	ret = t133a01_cmd_both(dev, T133A01_CMD_POF, &pof, 1);
	if (ret < 0) {
		return ret;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	return 0;
}

static int t133a01_write(const struct device *dev, const uint16_t x, const uint16_t y,
			 const struct display_buffer_descriptor *desc, const void *buf)
{
	struct display_buffer_descriptor scratch;
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	uint8_t ccset = 0x01;
	const uint8_t *src;
	size_t stride = DIV_ROUND_UP(config->width, 2U);
	size_t half_bytes = stride / 2U;
	size_t buf_len = stride * config->height;
	int ret;

	ret = display_color_dither_prepare(dev, &data->color_dither, &desc, &buf, &scratch);
	if (ret < 0) {
		return ret;
	}
	src = buf;

	if (x != 0U || y != 0U || desc->width != config->width || desc->height != config->height) {
		LOG_ERR("Partial updates not supported");
		return -ENOTSUP;
	}

	if (desc->pitch != desc->width) {
		LOG_ERR("Pitch must match width");
		return -EINVAL;
	}

	if (buf == NULL || desc->buf_size < buf_len) {
		LOG_ERR("Invalid buffer: %p (%zu < %zu)", buf, desc->buf_size, buf_len);
		return -EINVAL;
	}

	ret = t133a01_hw_init(dev);
	if (ret < 0) {
		return ret;
	}

	/* Latch the new colour set on both dies before sending image data. */
	ret = t133a01_cmd_both(dev, T133A01_CMD_CCSET, &ccset, 1);
	if (ret < 0) {
		goto out;
	}

	ret = t133a01_wait_until_ready(dev, T133A01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		goto out;
	}

	/* Left half to the master die, right half to the slave die. */
	ret = t133a01_push_half(dev, &config->dbi_master, src, stride, half_bytes);
	if (ret < 0) {
		goto out;
	}

	ret = t133a01_push_half(dev, &data->dbi_slave, src + half_bytes, stride, half_bytes);
	if (ret < 0) {
		goto out;
	}

	if (!data->blanking_on) {
		ret = t133a01_refresh(dev);
		if (ret < 0) {
			goto out;
		}

		ret = t133a01_deep_sleep(dev);
	}

out:
	mipi_dbi_release(config->mipi_dev, &config->dbi_master);
	return ret;
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
	struct t133a01_data *data = dev->data;

	memset(caps, 0, sizeof(*caps));
	memcpy(caps->color_palette, config->color_palette, sizeof(config->color_palette));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_I_4;
	caps->current_pixel_format = PIXEL_FORMAT_I_4;
	caps->screen_info = SCREEN_INFO_EPD;
	display_color_dither_patch_caps(&data->color_dither, caps);
}

static int t133a01_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	struct t133a01_data *data = dev->data;

	return display_color_dither_set_input_format(&data->color_dither, pf);
}

static int t133a01_init(const struct device *dev)
{
	const struct t133a01_config *config = dev->config;
	struct t133a01_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->mipi_dev)) {
		LOG_ERR_DEVICE_NOT_READY(config->mipi_dev);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("Busy GPIO not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->slave_cs)) {
		LOG_ERR("Slave CS GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->slave_cs, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure slave CS GPIO: %d", ret);
		return ret;
	}

	if (config->enable_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->enable_gpio)) {
			LOG_ERR("Enable GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->enable_gpio, GPIO_OUTPUT_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO: %d", ret);
			return ret;
		}
	}

	ret = gpio_pin_configure_dt(&config->busy_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure busy GPIO: %d", ret);
		return ret;
	}

	k_sem_init(&data->busy_sem, 0, 1);

	gpio_init_callback(&data->busy_cb, t133a01_busy_cb, BIT(config->busy_gpio.pin));
	ret = gpio_add_callback(config->busy_gpio.port, &data->busy_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add busy GPIO callback: %d", ret);
		return ret;
	}

	/* The slave die shares the bus with the master but uses its own CS. */
	data->dbi_slave = config->dbi_master;
	data->dbi_slave.config.cs.gpio = config->slave_cs;

	data->blanking_on = true;

	return t133a01_hw_init(dev);
}

#ifdef CONFIG_PM_DEVICE
static int t133a01_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		ret = t133a01_hw_init(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = t133a01_deep_sleep(dev);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif

static DEVICE_API(display, t133a01_api) = {
	.blanking_on = t133a01_blanking_on,
	.blanking_off = t133a01_blanking_off,
	.write = t133a01_write,
	.get_capabilities = t133a01_get_capabilities,
	.set_pixel_format = t133a01_set_pixel_format,
};

#define T133A01_DEFINE(inst)                                                                       \
	BUILD_ASSERT(CONFIG_DISPLAY_COLOR_PALETTE_MAX_SIZE >=                                      \
		     DT_PROP_LEN(DT_INST_CHILD(inst, color_palette), colors));                     \
	DISPLAY_COLOR_DITHER_DEFINE(inst);                                                         \
	static const struct t133a01_config t133a01_cfg_##inst = {                                  \
		.mipi_dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                   \
		.dbi_master =                                                                      \
			{                                                                          \
				.mode = MIPI_DBI_MODE_SPI_4WIRE,                                   \
				.config = MIPI_DBI_SPI_CONFIG_DT_INST(                             \
					inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),            \
			},                                                                         \
		.slave_cs = GPIO_DT_SPEC_INST_GET(inst, slave_cs_gpios),                           \
		.busy_gpio = GPIO_DT_SPEC_INST_GET(inst, busy_gpios),                              \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, enable_gpios, {0}),                  \
		.color_palette = DT_PROP(DT_INST_CHILD(inst, color_palette), colors),              \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
	};                                                                                         \
	static struct t133a01_data t133a01_data_##inst = {                                         \
		.color_dither = DISPLAY_COLOR_DITHER_INIT(inst),                                   \
	};                                                                                         \
	PM_DEVICE_DT_INST_DEFINE(inst, t133a01_pm_action);                                         \
	DEVICE_DT_INST_DEFINE(inst, t133a01_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &t133a01_data_##inst, &t133a01_cfg_##inst, POST_KERNEL,              \
			      CONFIG_DISPLAY_INIT_PRIORITY, &t133a01_api);

DT_INST_FOREACH_STATUS_OKAY(T133A01_DEFINE)
