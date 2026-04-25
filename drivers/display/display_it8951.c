/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ite_it8951

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

/* Timing constants (milliseconds) */
#define IT8951_RESET_DELAY_MS     200U
#define IT8951_RESET_WAIT_MS      200U
#define IT8951_BOOT_TIMEOUT_MS    5000U
#define IT8951_HRDY_TIMEOUT_MS    1000U
#define IT8951_REFRESH_TIMEOUT_MS 30000U

/*
 * SPI preambles.
 *
 * Every SPI transaction begins with a 16-bit big-endian preamble that
 * identifies the transaction type to the IT8951.
 */
#define IT8951_PREAMBLE_CMD 0x6000U /* write host command */
#define IT8951_PREAMBLE_WR  0x0000U /* write data word    */
#define IT8951_PREAMBLE_RD  0x1000U /* read data          */

/* Host interface commands */
#define IT8951_CMD_SYS_RUN      0x0001U /* wake / start execution        */
#define IT8951_CMD_STANDBY      0x0002U /* enter standby                 */
#define IT8951_CMD_REG_WR       0x0011U /* write internal register        */
#define IT8951_CMD_LD_IMG_AREA  0x0021U /* load image into area of buffer */
#define IT8951_CMD_LD_IMG_END   0x0022U /* finish image load              */
#define IT8951_CMD_DISPLAY_AREA 0x0034U /* trigger panel refresh          */
#define IT8951_CMD_GET_DEV_INFO 0x0302U /* read device-information struct */

/* Internal register addresses */
#define IT8951_REG_I80CPCR 0x0004U /* CPU panel command register */

/*
 * Load-image info word (IT8951_CMD_LD_IMG_AREA argument 0).
 *
 * Bits [3:0]  – endian type  (0 = little-endian word order)
 * Bits [7:4]  – pixel format (2 = 4 BPP)
 * Bits [11:8] – rotation     (0 = 0°)
 */
#define IT8951_LDIMG_L_ENDIAN 0x0U
#define IT8951_LDIMG_BPP_4    0x2U
#define IT8951_LDIMG_ROTATE_0 0x0U
#define IT8951_LDIMG_INFO_WORD                                                                     \
	(IT8951_LDIMG_L_ENDIAN | (IT8951_LDIMG_BPP_4 << 4U) | (IT8951_LDIMG_ROTATE_0 << 8U))

/*
 * GET_DEV_INFO response layout (20 × uint16_t words).
 *
 * Word 0  – panel width
 * Word 1  – panel height
 * Word 2  – image-buffer base address, low  16 bits
 * Word 3  – image-buffer base address, high 16 bits
 * Words 4–11  – firmware version string (8 × uint16_t, two ASCII chars each)
 * Words 12–19 – LUT version string
 */
#define IT8951_DEV_INFO_WORDS    20U
#define IT8951_DEV_INFO_W_IDX    0U
#define IT8951_DEV_INFO_H_IDX    1U
#define IT8951_DEV_INFO_ADRL_IDX 2U
#define IT8951_DEV_INFO_ADRH_IDX 3U

/* Total read buffer: 4-byte header (preamble + dummy) + 40 bytes of data */
#define IT8951_RD_HDR_LEN      4U
#define IT8951_DEV_INFO_RD_LEN (IT8951_RD_HDR_LEN + IT8951_DEV_INFO_WORDS * 2U)

struct it8951_config {
	struct spi_dt_spec spi;
	struct gpio_dt_spec hrdy_gpio;
	struct gpio_dt_spec reset_gpio;
	uint16_t width;
	uint16_t height;
	uint16_t waveform_mode;
};

struct it8951_data {
	bool blanking_on;
	struct k_sem hrdy_sem;
	struct gpio_callback hrdy_cb;
	uint32_t img_buf_base; /* image buffer base address from GET_DEV_INFO */
};

/* -------------------------------------------------------------------------
 * HRDY (hardware-ready) helpers
 * -------------------------------------------------------------------------
 */

static void it8951_hrdy_cb(const struct device *gpio_dev, struct gpio_callback *cb, uint32_t pins)
{
	struct it8951_data *data = CONTAINER_OF(cb, struct it8951_data, hrdy_cb);

	ARG_UNUSED(gpio_dev);
	ARG_UNUSED(pins);

	k_sem_give(&data->hrdy_sem);
}

/*
 * Wait until the IT8951 HRDY pin is asserted (high = ready).
 *
 * If the pin is already high the function returns immediately.  Otherwise it
 * arms a rising-edge interrupt and waits on a semaphore.
 */
static int it8951_wait_hrdy(const struct device *dev, uint32_t timeout_ms)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	int pin;
	int ret;

	pin = gpio_pin_get_dt(&config->hrdy_gpio);
	if (pin < 0) {
		LOG_ERR("Failed to read HRDY GPIO: %d", pin);
		return pin;
	}

	if (pin != 0) {
		return 0; /* already ready */
	}

	k_sem_reset(&data->hrdy_sem);

	ret = gpio_pin_interrupt_configure_dt(&config->hrdy_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure HRDY interrupt: %d", ret);
		return ret;
	}

	ret = k_sem_take(&data->hrdy_sem, K_MSEC(timeout_ms));

	gpio_pin_interrupt_configure_dt(&config->hrdy_gpio, GPIO_INT_DISABLE);

	if (ret < 0) {
		LOG_ERR("Timeout waiting for HRDY");
		return -ETIMEDOUT;
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Low-level SPI helpers
 * -------------------------------------------------------------------------
 */

/*
 * Write a host command to the IT8951.
 *
 * Waits for HRDY before asserting CS, then sends the 2-byte command preamble
 * (0x6000) followed by the 16-bit command word as a single SPI transaction.
 */
static int it8951_write_cmd(const struct device *dev, uint16_t cmd)
{
	const struct it8951_config *config = dev->config;
	uint8_t buf[4];
	const struct spi_buf tx = {.buf = buf, .len = sizeof(buf)};
	const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	int ret;

	sys_put_be16(IT8951_PREAMBLE_CMD, buf);
	sys_put_be16(cmd, buf + 2U);

	ret = it8951_wait_hrdy(dev, IT8951_HRDY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	return spi_write_dt(&config->spi, &tx_set);
}

/*
 * Write a single 16-bit data word to the IT8951.
 *
 * Waits for HRDY, then sends the data preamble (0x0000) followed by the word.
 */
static int it8951_write_data(const struct device *dev, uint16_t word)
{
	const struct it8951_config *config = dev->config;
	uint8_t buf[4];
	const struct spi_buf tx = {.buf = buf, .len = sizeof(buf)};
	const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	int ret;

	sys_put_be16(IT8951_PREAMBLE_WR, buf);
	sys_put_be16(word, buf + 2U);

	ret = it8951_wait_hrdy(dev, IT8951_HRDY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	return spi_write_dt(&config->spi, &tx_set);
}

/*
 * Write an internal IT8951 register.
 *
 * Sends REG_WR command followed by the register address and value as
 * individual data words (each preceded by HRDY wait + data preamble).
 */
static int it8951_write_reg(const struct device *dev, uint16_t reg, uint16_t val)
{
	int ret;

	ret = it8951_write_cmd(dev, IT8951_CMD_REG_WR);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, reg);
	if (ret < 0) {
		return ret;
	}

	return it8951_write_data(dev, val);
}

/*
 * Bulk-write pixel data to the IT8951's image buffer.
 *
 * Waits for HRDY once, then sends the data preamble (0x0000) and all pixel
 * bytes in a single SPI transaction (CS held low throughout).
 */
static int it8951_write_pixels(const struct device *dev, const void *pixels, size_t len)
{
	const struct it8951_config *config = dev->config;
	uint8_t preamble[2];
	const struct spi_buf tx_bufs[2] = {
		{.buf = preamble, .len = sizeof(preamble)},
		{.buf = (void *)pixels, .len = len},
	};
	const struct spi_buf_set tx_set = {.buffers = tx_bufs, .count = 2};
	int ret;

	sys_put_be16(IT8951_PREAMBLE_WR, preamble);

	ret = it8951_wait_hrdy(dev, IT8951_HRDY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	return spi_write_dt(&config->spi, &tx_set);
}

/*
 * Read the GET_DEV_INFO response (20 × uint16_t) from the IT8951.
 *
 * A single SPI transaction clocks out the read preamble (0x1000) + dummy word
 * while capturing 20 data words.  The received bytes are big-endian and are
 * converted to host-endian uint16_t values before returning.
 */
static int it8951_read_dev_info(const struct device *dev, uint16_t words[IT8951_DEV_INFO_WORDS])
{
	const struct it8951_config *config = dev->config;
	/*
	 * tx_buf:  [0x10, 0x00]  preamble
	 *          [0x00, 0x00]  dummy word
	 *          [0x00 × 40]   clock bytes (MOSI irrelevant during read)
	 *
	 * rx_buf:  first 4 bytes discarded (echo of preamble + dummy)
	 *          remaining 40 bytes contain the response words
	 */
	uint8_t tx_buf[IT8951_DEV_INFO_RD_LEN];
	uint8_t rx_buf[IT8951_DEV_INFO_RD_LEN];
	const struct spi_buf tx = {.buf = tx_buf, .len = sizeof(tx_buf)};
	struct spi_buf rx = {.buf = rx_buf, .len = sizeof(rx_buf)};
	const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	struct spi_buf_set rx_set = {.buffers = &rx, .count = 1};
	int ret;

	memset(tx_buf, 0, sizeof(tx_buf));
	sys_put_be16(IT8951_PREAMBLE_RD, tx_buf);
	/* tx_buf[2..3] already zero = dummy word */

	ret = it8951_wait_hrdy(dev, IT8951_HRDY_TIMEOUT_MS);
	if (ret < 0) {
		return ret;
	}

	ret = spi_transceive_dt(&config->spi, &tx_set, &rx_set);
	if (ret < 0) {
		return ret;
	}

	for (size_t i = 0U; i < IT8951_DEV_INFO_WORDS; i++) {
		words[i] = sys_get_be16(rx_buf + IT8951_RD_HDR_LEN + i * 2U);
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * IT8951 command sequences
 * -------------------------------------------------------------------------
 */

/*
 * Send SYS_RUN to the IT8951 without first waiting for HRDY.
 *
 * Used to wake the controller from standby mode, where HRDY is low and a
 * normal wait-then-send sequence would time out.  After transmission the
 * function waits for HRDY to confirm the controller is running.
 */
static int it8951_sys_run(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	uint8_t buf[4];
	const struct spi_buf tx = {.buf = buf, .len = sizeof(buf)};
	const struct spi_buf_set tx_set = {.buffers = &tx, .count = 1};
	int ret;

	sys_put_be16(IT8951_PREAMBLE_CMD, buf);
	sys_put_be16(IT8951_CMD_SYS_RUN, buf + 2U);

	/* Send without HRDY guard – controller may be in standby */
	ret = spi_write_dt(&config->spi, &tx_set);
	if (ret < 0) {
		return ret;
	}

	return it8951_wait_hrdy(dev, IT8951_HRDY_TIMEOUT_MS);
}

/*
 * Perform a full hardware initialisation of the IT8951.
 *
 * Sequence:
 *   1. Hardware reset (if reset-gpios is present).
 *   2. Wait for HRDY (controller boots its firmware).
 *   3. Send SYS_RUN.
 *   4. Enable the CPU-enhanced command interface (I80CPCR = 0x0001).
 *   5. Read GET_DEV_INFO to obtain the image-buffer base address.
 */
static int it8951_hw_init(const struct device *dev)
{
	const struct it8951_config *config = dev->config;
	struct it8951_data *data = dev->data;
	uint16_t dev_info[IT8951_DEV_INFO_WORDS];
	int ret;

	if (config->reset_gpio.port != NULL) {
		ret = gpio_pin_set_dt(&config->reset_gpio, 1); /* assert reset (active-low) */
		if (ret < 0) {
			LOG_ERR("Failed to assert reset GPIO: %d", ret);
			return ret;
		}
		k_msleep(IT8951_RESET_DELAY_MS);
		ret = gpio_pin_set_dt(&config->reset_gpio, 0); /* deassert reset */
		if (ret < 0) {
			LOG_ERR("Failed to deassert reset GPIO: %d", ret);
			return ret;
		}
		k_msleep(IT8951_RESET_WAIT_MS);
	}

	ret = it8951_wait_hrdy(dev, IT8951_BOOT_TIMEOUT_MS);
	if (ret < 0) {
		LOG_ERR("IT8951 did not become ready after reset");
		return ret;
	}

	ret = it8951_sys_run(dev);
	if (ret < 0) {
		LOG_ERR("SYS_RUN failed: %d", ret);
		return ret;
	}

	ret = it8951_write_reg(dev, IT8951_REG_I80CPCR, 0x0001U);
	if (ret < 0) {
		LOG_ERR("Failed to enable I80CPCR: %d", ret);
		return ret;
	}

	ret = it8951_write_cmd(dev, IT8951_CMD_GET_DEV_INFO);
	if (ret < 0) {
		LOG_ERR("GET_DEV_INFO command failed: %d", ret);
		return ret;
	}

	ret = it8951_read_dev_info(dev, dev_info);
	if (ret < 0) {
		LOG_ERR("Failed to read device info: %d", ret);
		return ret;
	}

	data->img_buf_base = ((uint32_t)dev_info[IT8951_DEV_INFO_ADRH_IDX] << 16U) |
			     dev_info[IT8951_DEV_INFO_ADRL_IDX];

	LOG_INF("IT8951 ready: panel %u x %u, img buf 0x%08x", dev_info[IT8951_DEV_INFO_W_IDX],
		dev_info[IT8951_DEV_INFO_H_IDX], data->img_buf_base);

	return 0;
}

/* -------------------------------------------------------------------------
 * Display driver API
 * -------------------------------------------------------------------------
 */

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

static int it8951_write(const struct device *dev, const uint16_t x, const uint16_t y,
			const struct display_buffer_descriptor *desc, const void *buf)
{
	const struct it8951_config *config = dev->config;
	const struct it8951_data *data = dev->data;
	size_t buf_len;
	int ret;

	if (x != 0U || y != 0U || desc->width != config->width ||
	    desc->height != config->height) {
		LOG_ERR("Partial updates not supported");
		return -ENOTSUP;
	}

	if (desc->pitch != desc->width) {
		LOG_ERR("Pitch must equal width");
		return -EINVAL;
	}

	/* 4 bits per pixel → 2 pixels per byte */
	buf_len = (size_t)config->width * config->height / 2U;

	if (buf == NULL || desc->buf_size < buf_len) {
		LOG_ERR("Invalid buffer: %p (size %zu < required %zu)", buf, desc->buf_size,
			buf_len);
		return -EINVAL;
	}

	/*
	 * Transfer pixel data into the IT8951's internal image buffer using the
	 * LD_IMG_AREA command.
	 *
	 * Argument layout (7 data words after the command):
	 *   Word 0 – LdImgInfo: endian | (bpp << 4) | (rotation << 8)
	 *   Word 1 – start frame-buffer address, low  16 bits
	 *   Word 2 – start frame-buffer address, high 16 bits
	 *   Word 3 – area X origin
	 *   Word 4 – area Y origin
	 *   Word 5 – area width
	 *   Word 6 – area height
	 * Followed by a bulk pixel-data write, then LD_IMG_END.
	 */
	ret = it8951_write_cmd(dev, IT8951_CMD_LD_IMG_AREA);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, IT8951_LDIMG_INFO_WORD);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, (uint16_t)(data->img_buf_base & 0xFFFFU));
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, (uint16_t)((data->img_buf_base >> 16U) & 0xFFFFU));
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, x);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, y);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, config->width);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_data(dev, config->height);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_pixels(dev, buf, buf_len);
	if (ret < 0) {
		return ret;
	}

	ret = it8951_write_cmd(dev, IT8951_CMD_LD_IMG_END);
	if (ret < 0) {
		return ret;
	}

	if (!data->blanking_on) {
		/*
		 * Trigger the panel refresh with the DISPLAY_AREA command.
		 *
		 * Arguments (5 data words):
		 *   Word 0 – area X origin
		 *   Word 1 – area Y origin
		 *   Word 2 – area width
		 *   Word 3 – area height
		 *   Word 4 – waveform mode
		 */
		ret = it8951_write_cmd(dev, IT8951_CMD_DISPLAY_AREA);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_write_data(dev, x);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_write_data(dev, y);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_write_data(dev, config->width);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_write_data(dev, config->height);
		if (ret < 0) {
			return ret;
		}

		ret = it8951_write_data(dev, config->waveform_mode);
		if (ret < 0) {
			return ret;
		}

		/* Wait for the e-paper panel to finish refreshing */
		ret = it8951_wait_hrdy(dev, IT8951_REFRESH_TIMEOUT_MS);
		if (ret < 0) {
			return ret;
		}
	}

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
		LOG_ERR("SPI bus not ready");
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&config->hrdy_gpio)) {
		LOG_ERR("HRDY GPIO not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->hrdy_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure HRDY GPIO: %d", ret);
		return ret;
	}

	k_sem_init(&data->hrdy_sem, 0, 1);

	gpio_init_callback(&data->hrdy_cb, it8951_hrdy_cb, BIT(config->hrdy_gpio.pin));
	ret = gpio_add_callback(config->hrdy_gpio.port, &data->hrdy_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add HRDY GPIO callback: %d", ret);
		return ret;
	}

	if (config->reset_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure reset GPIO: %d", ret);
			return ret;
		}
	}

	data->blanking_on = true;

	return it8951_hw_init(dev);
}

#ifdef CONFIG_PM_DEVICE
static int it8951_pm_action(const struct device *dev, enum pm_device_action action)
{
	int ret;

	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		/*
		 * Wake from standby.  HRDY may be low so SYS_RUN must be sent
		 * without the usual HRDY pre-check (it8951_sys_run handles this).
		 */
		ret = it8951_sys_run(dev);
		break;
	case PM_DEVICE_ACTION_SUSPEND:
		ret = it8951_write_cmd(dev, IT8951_CMD_STANDBY);
		break;
	default:
		ret = -ENOTSUP;
		break;
	}

	return ret;
}
#endif /* CONFIG_PM_DEVICE */

static DEVICE_API(display, it8951_api) = {
	.blanking_on = it8951_blanking_on,
	.blanking_off = it8951_blanking_off,
	.write = it8951_write,
	.get_capabilities = it8951_get_capabilities,
	.set_pixel_format = it8951_set_pixel_format,
};

#define IT8951_DEFINE(inst)                                                                        \
	static const struct it8951_config it8951_cfg_##inst = {                                    \
		.spi = SPI_DT_SPEC_INST_GET(inst, SPI_OP_MODE_MASTER | SPI_WORD_SET(8) |           \
						      SPI_TRANSFER_MSB,                            \
					  0),                                                       \
		.hrdy_gpio = GPIO_DT_SPEC_INST_GET(inst, hrdy_gpios),                             \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                   \
		.width = DT_INST_PROP(inst, width),                                                \
		.height = DT_INST_PROP(inst, height),                                              \
		.waveform_mode = DT_INST_PROP(inst, waveform_mode),                               \
	};                                                                                         \
	static struct it8951_data it8951_data_##inst;                                              \
	PM_DEVICE_DT_INST_DEFINE(inst, it8951_pm_action);                                          \
	DEVICE_DT_INST_DEFINE(inst, it8951_init, PM_DEVICE_DT_INST_GET(inst),                      \
			      &it8951_data_##inst, &it8951_cfg_##inst, POST_KERNEL,                \
			      CONFIG_DISPLAY_INIT_PRIORITY, &it8951_api);

DT_INST_FOREACH_STATUS_OKAY(IT8951_DEFINE)
