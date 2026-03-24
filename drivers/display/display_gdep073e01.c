/*
 * Copyright (c) 2026 Benjamin Lemouzy
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Controller register sequences derived from the Waveshare 7.3inch e-Paper (E) /
 * Good Display GDEP073E01 reference implementation (EPD_7in3e*.c).
 */

#define DT_DRV_COMPAT gooddisplay_gdep073e01

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/gdep073e01.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <string.h>

LOG_MODULE_REGISTER(gdep073e01, CONFIG_DISPLAY_LOG_LEVEL);

#define GDEP073E01_RESET_STABLE_MS   20U
#define GDEP073E01_RESET_PULSE_MS    2U
#define GDEP073E01_POST_RESET_MS     30U
#define GDEP073E01_BUSY_TIMEOUT_MS   5000U
#define GDEP073E01_REFRESH_TIMEOUT_MS 40000U
#define GDEP073E01_POWER_OFF_DELAY_MS 200U
#define GDEP073E01_DEEP_SLEEP_WAIT_MS 100U

#define GDEP073E01_CMD_CMDH            0xAAU
#define GDEP073E01_CMD_POWER_OFF       0x02U
#define GDEP073E01_CMD_POWER_ON        0x04U
#define GDEP073E01_CMD_BOOSTER         0x06U
#define GDEP073E01_CMD_DEEP_SLEEP      0x07U
#define GDEP073E01_CMD_DATA_START      0x10U
#define GDEP073E01_CMD_DISPLAY_REFRESH 0x12U
#define GDEP073E01_CMD_TRES            0x61U
#define GDEP073E01_CMD_E3              0xE3U
#define GDEP073E01_DEEP_SLEEP_MAGIC    0xA5U

#define GDEP073E01_PIXELS_PER_BYTE 2U

static const uint8_t gdep073e01_booster[] = {0x6F, 0x1F, 0x17, 0x49};

struct gdep073e01_config {
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_config;
	struct gpio_dt_spec busy_gpio;
	uint16_t width;
	uint16_t height;
};

struct gdep073e01_data {
	bool blanking_on;
	struct k_sem busy_sem;
	struct gpio_callback busy_cb;
};

static inline uint16_t gdep073e01_row_bytes(uint16_t width)
{
	return (uint16_t)((width % 2U == 0U) ? (width / 2U) : (width / 2U + 1U));
}

static inline int gdep073e01_write_cmd(const struct device *dev, uint8_t cmd, const uint8_t *data,
				       size_t len)
{
	const struct gdep073e01_config *cfg = dev->config;

	return mipi_dbi_command_write(cfg->mipi_dev, &cfg->dbi_config, cmd, data, len);
}

static void gdep073e01_busy_cb(const struct device *gpio_dev, struct gpio_callback *cb,
			       uint32_t pins)
{
	struct gdep073e01_data *data = CONTAINER_OF(cb, struct gdep073e01_data, busy_cb);

	ARG_UNUSED(gpio_dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->busy_sem);
}

static int gdep073e01_busy_wait(const struct device *dev, uint32_t timeout_ms)
{
	const struct gdep073e01_config *cfg = dev->config;
	struct gdep073e01_data *data = dev->data;
	int ret;

	if (cfg->busy_gpio.port == NULL) {
		LOG_WRN("Busy GPIO not configured, using fixed delay");
		k_msleep(timeout_ms);
		return 0;
	}

	if (!device_is_ready(cfg->busy_gpio.port)) {
		LOG_WRN("Busy GPIO port not ready, using fixed delay");
		k_msleep(timeout_ms);
		return 0;
	}

	if (gpio_pin_get_dt(&cfg->busy_gpio) != 0) {
		return 0;
	}

	k_sem_reset(&data->busy_sem);

	ret = gpio_pin_interrupt_configure_dt(&cfg->busy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure busy GPIO interrupt: %d", ret);
		return ret;
	}

	ret = k_sem_take(&data->busy_sem, K_MSEC(timeout_ms));

	gpio_pin_interrupt_configure_dt(&cfg->busy_gpio, GPIO_INT_DISABLE);

	if (ret < 0) {
		LOG_ERR("Timeout waiting for busy signal");
		return -ETIMEDOUT;
	}

	return 0;
}

static int gdep073e01_reset_pulse(const struct device *dev)
{
	const struct gdep073e01_config *cfg = dev->config;
	int ret;

	k_msleep(GDEP073E01_RESET_STABLE_MS);
	ret = mipi_dbi_reset(cfg->mipi_dev, GDEP073E01_RESET_PULSE_MS);
	if (ret < 0) {
		return ret;
	}
	k_msleep(GDEP073E01_RESET_STABLE_MS);
	return 0;
}

static int gdep073e01_turn_on_display(const struct device *dev)
{
	static const uint8_t refdata[] = {0x00};
	static const uint8_t pofdata[] = {0x00};
	int ret;

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_POWER_ON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	ret = gdep073e01_busy_wait(dev, GDEP073E01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_BOOSTER, gdep073e01_booster,
				   sizeof(gdep073e01_booster));
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_DISPLAY_REFRESH, refdata, sizeof(refdata));
	if (ret < 0) {
		return ret;
	}
	ret = gdep073e01_busy_wait(dev, GDEP073E01_REFRESH_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_POWER_OFF, pofdata, sizeof(pofdata));
	if (ret < 0) {
		return ret;
	}
	return gdep073e01_busy_wait(dev, GDEP073E01_BUSY_TIMEOUT_MS);
}

static int gdep073e01_deep_sleep(const struct device *dev)
{
	static const uint8_t pof[] = {0x00};
	static const uint8_t dsl[] = {GDEP073E01_DEEP_SLEEP_MAGIC};
	int ret;

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_POWER_OFF, pof, sizeof(pof));
	if (ret < 0) {
		return ret;
	}
	ret = gdep073e01_busy_wait(dev, GDEP073E01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_DEEP_SLEEP, dsl, sizeof(dsl));
	if (ret < 0) {
		return ret;
	}

	k_msleep(GDEP073E01_DEEP_SLEEP_WAIT_MS);
	return 0;
}

static int gdep073e01_hw_init(const struct device *dev)
{
	const struct gdep073e01_config *cfg = dev->config;
	int ret;

	ret = gdep073e01_reset_pulse(dev);
	if (ret < 0) {
		LOG_ERR("Reset failed: %d", ret);
		return ret;
	}

	ret = gdep073e01_busy_wait(dev, GDEP073E01_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}
	k_msleep(GDEP073E01_POST_RESET_MS);

	static const uint8_t cmdh[] = {0x49, 0x55, 0x20, 0x08, 0x09, 0x18};

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_CMDH, cmdh, sizeof(cmdh));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r01[] = {0x3F};

	ret = gdep073e01_write_cmd(dev, 0x01, r01, sizeof(r01));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r00[] = {0x5F, 0x69};

	ret = gdep073e01_write_cmd(dev, 0x00, r00, sizeof(r00));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r03[] = {0x00, 0x54, 0x00, 0x44};

	ret = gdep073e01_write_cmd(dev, 0x03, r03, sizeof(r03));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r05[] = {0x40, 0x1F, 0x1F, 0x2C};

	ret = gdep073e01_write_cmd(dev, 0x05, r05, sizeof(r05));
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_BOOSTER, gdep073e01_booster,
				   sizeof(gdep073e01_booster));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r08[] = {0x6F, 0x1F, 0x1F, 0x22};

	ret = gdep073e01_write_cmd(dev, 0x08, r08, sizeof(r08));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r30[] = {0x03};

	ret = gdep073e01_write_cmd(dev, 0x30, r30, sizeof(r30));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r50[] = {0x3F};

	ret = gdep073e01_write_cmd(dev, 0x50, r50, sizeof(r50));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r60[] = {0x02, 0x00};

	ret = gdep073e01_write_cmd(dev, 0x60, r60, sizeof(r60));
	if (ret < 0) {
		return ret;
	}

	uint8_t tres[] = {
		(cfg->width >> 8) & 0xFF,
		cfg->width & 0xFF,
		(cfg->height >> 8) & 0xFF,
		cfg->height & 0xFF,
	};

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_TRES, tres, sizeof(tres));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t r84[] = {0x01};

	ret = gdep073e01_write_cmd(dev, 0x84, r84, sizeof(r84));
	if (ret < 0) {
		return ret;
	}

	static const uint8_t re3[] = {0x2F};

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_E3, re3, sizeof(re3));
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_POWER_ON, NULL, 0);
	if (ret < 0) {
		return ret;
	}
	return gdep073e01_busy_wait(dev, GDEP073E01_BUSY_TIMEOUT_MS);
}

static int gdep073e01_init(const struct device *dev)
{
	const struct gdep073e01_config *cfg = dev->config;
	struct gdep073e01_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->mipi_dev)) {
		LOG_ERR("MIPI DBI device not ready");
		return -ENODEV;
	}

	if (cfg->busy_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&cfg->busy_gpio)) {
			LOG_ERR("Busy GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->busy_gpio, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("Failed to configure busy GPIO: %d", ret);
			return ret;
		}

		k_sem_init(&data->busy_sem, 0, 1);
		gpio_init_callback(&data->busy_cb, gdep073e01_busy_cb, BIT(cfg->busy_gpio.pin));
		ret = gpio_add_callback(cfg->busy_gpio.port, &data->busy_cb);
		if (ret < 0) {
			LOG_ERR("Failed to add busy GPIO callback: %d", ret);
			return ret;
		}
	}

	data->blanking_on = true;

	ret = gdep073e01_hw_init(dev);
	if (ret < 0) {
		LOG_ERR("Hardware init failed: %d", ret);
		return ret;
	}

	LOG_INF("GDEP073E01 initialized (%ux%u)", cfg->width, cfg->height);
	return 0;
}

static int gdep073e01_write(const struct device *dev, const uint16_t x, const uint16_t y,
			    const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct gdep073e01_config *cfg = dev->config;
	struct gdep073e01_data *data = dev->data;
	const uint16_t row_bytes = gdep073e01_row_bytes(cfg->width);
	const size_t buf_len = (size_t)row_bytes * cfg->height;
	int ret;

	if (x != 0U || y != 0U || desc->width != cfg->width || desc->height != cfg->height) {
		LOG_ERR("Partial updates not supported");
		return -ENOTSUP;
	}

	if (buf == NULL || desc->buf_size < buf_len) {
		LOG_ERR("Invalid buffer size %u (need %zu)", desc->buf_size, buf_len);
		return -EINVAL;
	}

	ret = gdep073e01_hw_init(dev);
	if (ret < 0) {
		return ret;
	}

	ret = gdep073e01_write_cmd(dev, GDEP073E01_CMD_DATA_START, buf, buf_len);
	if (ret < 0) {
		return ret;
	}

	if (!data->blanking_on) {
		ret = gdep073e01_turn_on_display(dev);
		if (ret < 0) {
			return ret;
		}

		k_msleep(GDEP073E01_POWER_OFF_DELAY_MS);

		ret = gdep073e01_deep_sleep(dev);
		if (ret < 0) {
			LOG_WRN("Deep sleep failed: %d", ret);
		}
	}

	return mipi_dbi_release(cfg->mipi_dev, &cfg->dbi_config);
}

static int gdep073e01_blanking_on(const struct device *dev)
{
	struct gdep073e01_data *data = dev->data;

	data->blanking_on = true;
	return 0;
}

static int gdep073e01_blanking_off(const struct device *dev)
{
	struct gdep073e01_data *data = dev->data;

	data->blanking_on = false;
	return 0;
}

static void gdep073e01_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct gdep073e01_config *cfg = dev->config;

	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = cfg->width;
	caps->y_resolution = cfg->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_L_4;
	caps->current_pixel_format = PIXEL_FORMAT_L_4;
	caps->screen_info = SCREEN_INFO_EPD;
}

static int gdep073e01_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	ARG_UNUSED(dev);

	if (pf == PIXEL_FORMAT_L_4) {
		return 0;
	}

	return -ENOTSUP;
}

#ifdef CONFIG_PM_DEVICE
static int gdep073e01_pm_action(const struct device *dev, enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return gdep073e01_hw_init(dev);
	case PM_DEVICE_ACTION_SUSPEND:
		return gdep073e01_deep_sleep(dev);
	default:
		return -ENOTSUP;
	}
}
#endif

static DEVICE_API(display, gdep073e01_api) = {
	.blanking_on = gdep073e01_blanking_on,
	.blanking_off = gdep073e01_blanking_off,
	.write = gdep073e01_write,
	.get_capabilities = gdep073e01_get_capabilities,
	.set_pixel_format = gdep073e01_set_pixel_format,
};

#define GDEP073E01_DEFINE(inst)                                                                      \
	static const struct gdep073e01_config gdep073e01_cfg_##inst = {                              \
		.mipi_dev = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                   \
		.dbi_config =                                                                          \
			{                                                                              \
				.mode = MIPI_DBI_MODE_SPI_4WIRE,                                       \
				.config = MIPI_DBI_SPI_CONFIG_DT_INST(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0), \
			},                                                                             \
		.busy_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, busy_gpios, {0}),                          \
		.width = DT_INST_PROP(inst, width),                                                    \
		.height = DT_INST_PROP(inst, height),                                                  \
	};                                                                                             \
	static struct gdep073e01_data gdep073e01_data_##inst;                                          \
	PM_DEVICE_DT_INST_DEFINE(inst, gdep073e01_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, gdep073e01_init, PM_DEVICE_DT_INST_GET(inst),                     \
			      &gdep073e01_data_##inst, &gdep073e01_cfg_##inst, POST_KERNEL,            \
			      CONFIG_DISPLAY_INIT_PRIORITY, &gdep073e01_api);

DT_INST_FOREACH_STATUS_OKAY(GDEP073E01_DEFINE)
</think>
Fixing a compile error: `booster` was referenced before definition in `gdep073e01_hw_init`.

<｜tool▁calls▁begin｜><｜tool▁call▁begin｜>
StrReplace