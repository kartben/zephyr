/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/color_dither.h>
#include <zephyr/drivers/display/ed2208-gca.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/mipi_dbi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>

LOG_MODULE_REGISTER(ed2208_gca, CONFIG_DISPLAY_LOG_LEVEL);

/*
 * Driver for the ED2208-GCA family of Spectra 6 e-ink display controllers.
 *
 * Two panel variants are supported:
 *
 * - ED2208-GCA (EL073TF1): single controller driving the whole panel.
 * - T133A01: two cascaded controller dies sharing the SPI clock, data and
 *   data/command lines. Each die has its own chip-select and drives one half
 *   of the panel. The "master" die chip-select comes from the parent MIPI DBI
 *   SPI controller; the "slave" die chip-select is driven directly by this
 *   driver (slave-cs-gpios) so that commands can be broadcast to both dies at
 *   once by holding both chip-selects active.
 */

#define ED2208_GCA_RESET_DELAY_MS    20U
#define ED2208_GCA_BUSY_TIMEOUT_MS   5000U  /* 5 seconds for normal operations */
#define ED2208_GCA_REFRESH_TIMEOUTMS 40000U /* 40 seconds for display refresh */
#define ED2208_GCA_TX_CHUNK_SIZE     256U

#define T133A01_POWER_DELAY_MS 30U

#define ED2208_GCA_CMD_PSR          0x00
#define ED2208_GCA_CMD_CMDH         0xAA
#define ED2208_GCA_CMD_PWRR         0x01
#define ED2208_GCA_CMD_POF          0x02
#define ED2208_GCA_CMD_POFS         0x03
#define ED2208_GCA_CMD_PON          0x04
#define ED2208_GCA_CMD_BTST1        0x05
#define ED2208_GCA_CMD_BTST2        0x06
#define ED2208_GCA_CMD_DSLP         0x07
#define ED2208_GCA_CMD_BTST3        0x08
#define ED2208_GCA_CMD_DTM          0x10
#define ED2208_GCA_CMD_IPC          0x13
#define ED2208_GCA_CMD_PLL          0x30
#define ED2208_GCA_CMD_TSE          0x41
#define ED2208_GCA_CMD_CDI          0x50
#define ED2208_GCA_CMD_TCON         0x60
#define ED2208_GCA_CMD_TRES         0x61
#define ED2208_GCA_CMD_VDCS         0x82
#define ED2208_GCA_CMD_T_VDCS       0x84
#define ED2208_GCA_CMD_AGID         0x86
#define ED2208_GCA_CMD_DRF          0x12
#define ED2208_GCA_CMD_CCSET        0xE0
#define ED2208_GCA_CMD_PWS          0xE3
#define ED2208_GCA_CMD_TSSET        0xE6
#define ED2208_GCA_DEEP_SLEEP_CHECK 0xA5

/* Target of a command: the master (or only) die, or a broadcast to both dies. */
#define ED2208_GCA_TGT_MASTER 0U
#define ED2208_GCA_TGT_BOTH   1U

struct ed2208_gca_quirks {
	/* Delay after releasing reset before talking to the controller. */
	uint16_t reset_wait_ms;
	/* Delay after each init-sequence command. */
	uint16_t init_cmd_delay_ms;
	/* Wait for BUSY_N before running the init sequence. */
	bool wait_before_init_seq;
	/* Wait for BUSY_N after the init sequence (when it ends with PON). */
	bool wait_after_init_seq;
	int (*run_init_seq)(const struct device *dev);
	int (*write_frame)(const struct device *dev, const uint8_t *buf);
	int (*refresh)(const struct device *dev);
};

struct ed2208_gca_config {
	const struct ed2208_gca_quirks *quirks;
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_config;
	struct gpio_dt_spec busy_gpio;
	/* Chip-select of the second controller die, dual-die panels only. */
	struct gpio_dt_spec slave_cs;
	struct gpio_dt_spec enable_gpio;
	uint32_t color_palette[ED2208_GCA_COLOR_PALETTE_SIZE];
	uint16_t width;
	uint16_t height;
};

struct ed2208_gca_data {
	struct display_color_dither_state color_dither;
	/* Copy of dbi_config with the chip-select overridden to the slave die. */
	struct mipi_dbi_config dbi_slave;
	bool blanking_on;
	struct k_sem busy_sem;
	struct gpio_callback busy_cb;
};

/*
 * Send a command (with optional data) to the master die, or to both dies at
 * once. The master die is selected by the MIPI DBI transfer; on broadcast the
 * slave die is selected in parallel by holding its chip-select active for the
 * duration of the transfer.
 */
static int ed2208_gca_write_cmd(const struct device *dev, uint8_t target, uint8_t cmd,
				const uint8_t *data, size_t len)
{
	const struct ed2208_gca_config *config = dev->config;
	bool both = (target == ED2208_GCA_TGT_BOTH) && (config->slave_cs.port != NULL);
	int ret;

	if (both) {
		gpio_pin_set_dt(&config->slave_cs, 1);
	}

	ret = mipi_dbi_command_write(config->mipi_dev, &config->dbi_config, cmd, data, len);

	if (both) {
		gpio_pin_set_dt(&config->slave_cs, 0);
	}

	return ret;
}

static inline int ed2208_gca_write_cmd_uint8(const struct device *dev, uint8_t target, uint8_t cmd,
					     uint8_t data)
{
	return ed2208_gca_write_cmd(dev, target, cmd, &data, 1);
}

/* Run a packed init sequence of {target, cmd, len, bytes...} tuples. */
static int ed2208_gca_run_init_sequence(const struct device *dev, const uint8_t *seq,
					size_t seq_len)
{
	const struct ed2208_gca_config *config = dev->config;
	size_t i = 0;

	while (i < seq_len) {
		uint8_t target = seq[i++];
		uint8_t cmd = seq[i++];
		uint8_t len = seq[i++];
		int ret = ed2208_gca_write_cmd(dev, target, cmd, len ? &seq[i] : NULL, len);

		if (ret < 0) {
			return ret;
		}
		i += len;

		if (config->quirks->init_cmd_delay_ms > 0) {
			k_msleep(config->quirks->init_cmd_delay_ms);
		}
	}

	return 0;
}

static void ed2208_gca_busy_cb(const struct device *gpio_dev, struct gpio_callback *cb,
			       uint32_t pins)
{
	struct ed2208_gca_data *data = CONTAINER_OF(cb, struct ed2208_gca_data, busy_cb);

	ARG_UNUSED(gpio_dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->busy_sem);
}

/* Wait until BUSY_N indicates the panel is idle. */
static int ed2208_gca_wait_until_ready(const struct device *dev, uint32_t timeout_ms)
{
	const struct ed2208_gca_config *config = dev->config;
	struct ed2208_gca_data *data = dev->data;
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

static int ed2208_gca_hw_init(const struct device *dev)
{
	const struct ed2208_gca_config *config = dev->config;
	const struct ed2208_gca_quirks *quirks = config->quirks;
	int ret;

	ret = mipi_dbi_reset(config->mipi_dev, ED2208_GCA_RESET_DELAY_MS);
	if (ret < 0) {
		LOG_ERR("Failed to reset display: %d", ret);
		return ret;
	}

	k_msleep(quirks->reset_wait_ms);

	if (quirks->wait_before_init_seq) {
		ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
		if (ret < 0) {
			goto out;
		}
	}

	ret = quirks->run_init_seq(dev);
	if (ret < 0) {
		goto out;
	}

	if (quirks->wait_after_init_seq) {
		ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
	}

out:
	mipi_dbi_release(config->mipi_dev, &config->dbi_config);
	return ret;
}

static int ed2208_gca_deep_sleep(const struct device *dev)
{
	/* On dual-die panels only the master die is put into deep sleep. */
	return ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_DSLP,
					  ED2208_GCA_DEEP_SLEEP_CHECK);
}

/*
 * ED2208-GCA (single die) variant.
 */

static int ed2208_gca_variant_run_init_seq(const struct device *dev)
{
	const struct ed2208_gca_config *config = dev->config;
	const uint8_t init_seq[] = {
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_CMDH,   6, 0x49, 0x55, 0x20, 0x08, 0x09,
								 0x18,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PWRR,   6, 0x3F, 0x00, 0x32, 0x2A, 0x0E,
								 0x2A,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PSR,    2, 0x5F, 0x69,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_POFS,   4, 0x00, 0x54, 0x00, 0x44,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_BTST1,  4, 0x40, 0x1F, 0x1F, 0x2C,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_BTST2,  4, 0x6F, 0x1F, 0x16, 0x25,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_BTST3,  4, 0x6F, 0x1F, 0x1F, 0x22,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_IPC,    2, 0x00, 0x04,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PLL,    1, 0x02,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_TSE,    1, 0x00,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_CDI,    1, 0x3F,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_TCON,   2, 0x02, 0x00,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_TRES,   4, (config->width  >> 8) & 0xFF,
								 config->width  & 0xFF,
								 (config->height >> 8) & 0xFF,
								 config->height & 0xFF,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_VDCS,   1, 0x1E,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_T_VDCS, 1, 0x01,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_AGID,   1, 0x00,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PWS,    1, 0x2F,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_CCSET,  1, 0x00,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_TSSET,  1, 0x00,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PON,    0,
	};

	return ed2208_gca_run_init_sequence(dev, init_seq, sizeof(init_seq));
}

static int ed2208_gca_variant_write_frame(const struct device *dev, const uint8_t *buf)
{
	const struct ed2208_gca_config *config = dev->config;
	struct display_buffer_descriptor mipi_desc = {
		.height = 1,
	};
	size_t buf_len = DIV_ROUND_UP(config->width, 2U) * config->height;
	size_t offset = 0U;
	int ret;

	ret = ed2208_gca_write_cmd(dev, ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_DTM, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	while (offset < buf_len) {
		size_t chunk_len = MIN(ED2208_GCA_TX_CHUNK_SIZE, buf_len - offset);

		mipi_desc.buf_size = chunk_len;
		mipi_desc.width = chunk_len;
		mipi_desc.pitch = chunk_len;
		mipi_desc.frame_incomplete = (offset + chunk_len) != buf_len;

		ret = mipi_dbi_write_display(config->mipi_dev, &config->dbi_config, buf + offset,
					     &mipi_desc, PIXEL_FORMAT_I_4);
		if (ret < 0) {
			return ret;
		}

		offset += chunk_len;
	}

	return 0;
}

static int ed2208_gca_variant_refresh(const struct device *dev)
{
	int ret;

	ret = ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_DRF, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_REFRESH_TIMEOUTMS);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_POF, 0x00);
	if (ret < 0) {
		return ret;
	}

	return ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
}

/*
 * T133A01 (dual die) variant.
 */

static int t133a01_run_init_seq(const struct device *dev)
{
	const struct ed2208_gca_config *config = dev->config;
	const uint16_t die_height = config->height / 2U;
	const uint8_t init_seq[] = {
		ED2208_GCA_TGT_MASTER, 0x74, 9, 0x00, 0x0C, 0x0C, 0xD9, 0xDD, 0xDD, 0x15, 0x15,
						0x55,
		ED2208_GCA_TGT_BOTH,   0xF0, 6, 0x49, 0x55, 0x13, 0x5D, 0x05, 0x10,
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_PSR, 2, 0xDF, 0x69,
		ED2208_GCA_TGT_MASTER, 0xA5, 3, 0x44, 0x54, 0x00,
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_CDI, 1, 0x37,
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_TCON, 2, 0x03, 0x03,
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_AGID, 1, 0x10,
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_PWS, 1, 0x22,
		/* Per-die resolution: panel width x (panel height / 2). */
		ED2208_GCA_TGT_BOTH,   ED2208_GCA_CMD_TRES, 4, (config->width >> 8) & 0xFF,
							       config->width & 0xFF,
							       (die_height >> 8) & 0xFF,
							       die_height & 0xFF,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_PWRR, 6, 0x0F, 0x00, 0x28, 0x2C, 0x28, 0x38,
		ED2208_GCA_TGT_MASTER, 0xB6, 1, 0x07,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_BTST2, 2, 0xE0, 0x20,
		ED2208_GCA_TGT_MASTER, 0xB7, 1, 0x01,
		ED2208_GCA_TGT_MASTER, ED2208_GCA_CMD_BTST1, 2, 0xE0, 0x20,
		ED2208_GCA_TGT_MASTER, 0xB0, 1, 0x01,
		ED2208_GCA_TGT_MASTER, 0xB1, 1, 0x02,
	};

	return ed2208_gca_run_init_sequence(dev, init_seq, sizeof(init_seq));
}

/* Stream one half-panel worth of pixel data to a single die. */
static int t133a01_push_half(const struct device *dev, const struct mipi_dbi_config *dbi,
			     const uint8_t *src, size_t stride, size_t half_bytes)
{
	const struct ed2208_gca_config *config = dev->config;
	struct display_buffer_descriptor desc = {
		.height = 1,
		.width = half_bytes,
		.pitch = half_bytes,
		.buf_size = half_bytes,
	};
	int ret;

	ret = mipi_dbi_command_write(config->mipi_dev, dbi, ED2208_GCA_CMD_DTM, NULL, 0);
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

static int t133a01_write_frame(const struct device *dev, const uint8_t *buf)
{
	const struct ed2208_gca_config *config = dev->config;
	struct ed2208_gca_data *data = dev->data;
	size_t stride = DIV_ROUND_UP(config->width, 2U);
	size_t half_bytes = stride / 2U;
	int ret;

	/* Latch the new colour set on both dies before sending image data. */
	ret = ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_BOTH, ED2208_GCA_CMD_CCSET, 0x01);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	/* Left half to the master die, right half to the slave die. */
	ret = t133a01_push_half(dev, &config->dbi_config, buf, stride, half_bytes);
	if (ret < 0) {
		return ret;
	}

	return t133a01_push_half(dev, &data->dbi_slave, buf + half_bytes, stride, half_bytes);
}

static int t133a01_refresh(const struct device *dev)
{
	int ret;

	ret = ed2208_gca_write_cmd(dev, ED2208_GCA_TGT_BOTH, ED2208_GCA_CMD_PON, NULL, 0);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	ret = ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_BOTH, ED2208_GCA_CMD_DRF, 0x01);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_REFRESH_TIMEOUTMS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	ret = ed2208_gca_write_cmd_uint8(dev, ED2208_GCA_TGT_BOTH, ED2208_GCA_CMD_POF, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = ed2208_gca_wait_until_ready(dev, ED2208_GCA_BUSY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	k_msleep(T133A01_POWER_DELAY_MS);

	return 0;
}

/*
 * Common display API implementation.
 */

static int ed2208_gca_write(const struct device *dev, const uint16_t x, const uint16_t y,
			    const struct display_buffer_descriptor *desc, const void *buf)
{
	struct display_buffer_descriptor scratch;
	const struct ed2208_gca_config *config = dev->config;
	struct ed2208_gca_data *data = dev->data;
	size_t buf_len = DIV_ROUND_UP(config->width, 2U) * config->height;
	int ret;

	ret = display_color_dither_prepare(dev, &data->color_dither, &desc, &buf, &scratch);
	if (ret < 0) {
		return ret;
	}

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

	ret = ed2208_gca_hw_init(dev);
	if (ret < 0) {
		return ret;
	}

	ret = config->quirks->write_frame(dev, buf);
	if (ret < 0) {
		goto out;
	}

	if (!data->blanking_on) {
		ret = config->quirks->refresh(dev);
		if (ret < 0) {
			goto out;
		}

		ret = ed2208_gca_deep_sleep(dev);
	}

out:
	mipi_dbi_release(config->mipi_dev, &config->dbi_config);
	return ret;
}

static int ed2208_gca_blanking_on(const struct device *dev)
{
	struct ed2208_gca_data *data = dev->data;

	data->blanking_on = true;
	return 0;
}

static int ed2208_gca_blanking_off(const struct device *dev)
{
	struct ed2208_gca_data *data = dev->data;

	data->blanking_on = false;
	return 0;
}

static void ed2208_gca_get_capabilities(const struct device *dev, struct display_capabilities *caps)
{
	const struct ed2208_gca_config *config = dev->config;
	struct ed2208_gca_data *data = dev->data;

	memset(caps, 0, sizeof(*caps));
	memcpy(caps->color_palette, config->color_palette, sizeof(config->color_palette));
	caps->x_resolution = config->width;
	caps->y_resolution = config->height;
	caps->supported_pixel_formats = PIXEL_FORMAT_I_4;
	caps->current_pixel_format = PIXEL_FORMAT_I_4;
	caps->screen_info = SCREEN_INFO_EPD;
	display_color_dither_patch_caps(&data->color_dither, caps);
}

static int ed2208_gca_set_pixel_format(const struct device *dev, const enum display_pixel_format pf)
{
	struct ed2208_gca_data *data = dev->data;

	return display_color_dither_set_input_format(&data->color_dither, pf);
}

static int ed2208_gca_init(const struct device *dev)
{
	const struct ed2208_gca_config *config = dev->config;
	struct ed2208_gca_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->mipi_dev)) {
		LOG_ERR_DEVICE_NOT_READY(config->mipi_dev);
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->busy_gpio)) {
		LOG_ERR("Busy GPIO not ready");
		return -ENODEV;
	}

	if (config->slave_cs.port != NULL) {
		if (!gpio_is_ready_dt(&config->slave_cs)) {
			LOG_ERR("Slave CS GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->slave_cs, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure slave CS GPIO: %d", ret);
			return ret;
		}

		/* The slave die shares the bus with the master but uses its own CS. */
		data->dbi_slave = config->dbi_config;
		data->dbi_slave.config.cs.gpio = config->slave_cs;
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

	gpio_init_callback(&data->busy_cb, ed2208_gca_busy_cb, BIT(config->busy_gpio.pin));
	ret = gpio_add_callback(config->busy_gpio.port, &data->busy_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add busy GPIO callback: %d", ret);
		return ret;
	}

	data->blanking_on = true;

	return ed2208_gca_hw_init(dev);
}

#ifdef CONFIG_PM_DEVICE
static int ed2208_gca_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		ret = ed2208_gca_hw_init(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = ed2208_gca_deep_sleep(dev);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif

static DEVICE_API(display, ed2208_gca_api) = {
	.blanking_on = ed2208_gca_blanking_on,
	.blanking_off = ed2208_gca_blanking_off,
	.write = ed2208_gca_write,
	.get_capabilities = ed2208_gca_get_capabilities,
	.set_pixel_format = ed2208_gca_set_pixel_format,
};

static const struct ed2208_gca_quirks ed2208_gca_quirks = {
	.reset_wait_ms = 10U,
	.init_cmd_delay_ms = 0U,
	.wait_before_init_seq = false,
	.wait_after_init_seq = true,
	.run_init_seq = ed2208_gca_variant_run_init_seq,
	.write_frame = ed2208_gca_variant_write_frame,
	.refresh = ed2208_gca_variant_refresh,
};

static const struct ed2208_gca_quirks t133a01_quirks = {
	.reset_wait_ms = 20U,
	.init_cmd_delay_ms = 10U,
	.wait_before_init_seq = true,
	.wait_after_init_seq = false,
	.run_init_seq = t133a01_run_init_seq,
	.write_frame = t133a01_write_frame,
	.refresh = t133a01_refresh,
};

#define ED2208_GCA_DEFINE(n, quirks_ptr)                                                           \
	BUILD_ASSERT(CONFIG_DISPLAY_COLOR_PALETTE_MAX_SIZE >=                                      \
		     DT_PROP_LEN(DT_CHILD(n, color_palette), colors));                             \
	DISPLAY_COLOR_DITHER_DEFINE_NODE(n);                                                       \
	static const struct ed2208_gca_config ed2208_gca_cfg_##n = {                               \
		.quirks = (quirks_ptr),                                                            \
		.mipi_dev = DEVICE_DT_GET(DT_PARENT(n)),                                           \
		.dbi_config =                                                                      \
			{                                                                          \
				.mode = MIPI_DBI_MODE_SPI_4WIRE,                                   \
				.config = MIPI_DBI_SPI_CONFIG_DT(                                  \
					n, SPI_OP_MODE_MASTER | SPI_WORD_SET(8), 0),               \
			},                                                                         \
		.busy_gpio = GPIO_DT_SPEC_GET(n, busy_gpios),                                      \
		.slave_cs = GPIO_DT_SPEC_GET_OR(n, slave_cs_gpios, {0}),                           \
		.enable_gpio = GPIO_DT_SPEC_GET_OR(n, enable_gpios, {0}),                          \
		.color_palette = DT_PROP(DT_CHILD(n, color_palette), colors),                      \
		.width = DT_PROP(n, width),                                                        \
		.height = DT_PROP(n, height),                                                      \
	};                                                                                         \
	static struct ed2208_gca_data ed2208_gca_data_##n = {                                      \
		.color_dither = DISPLAY_COLOR_DITHER_INIT_NODE(n),                                 \
	};                                                                                         \
	PM_DEVICE_DT_DEFINE(n, ed2208_gca_pm_action);                                              \
	DEVICE_DT_DEFINE(n, ed2208_gca_init, PM_DEVICE_DT_GET(n), &ed2208_gca_data_##n,            \
			 &ed2208_gca_cfg_##n, POST_KERNEL, CONFIG_DISPLAY_INIT_PRIORITY,           \
			 &ed2208_gca_api);

DT_FOREACH_STATUS_OKAY_VARGS(eink_ed2208_gca, ED2208_GCA_DEFINE, &ed2208_gca_quirks)
DT_FOREACH_STATUS_OKAY_VARGS(eink_t133a01, ED2208_GCA_DEFINE, &t133a01_quirks)
