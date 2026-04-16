/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/display/ac057tc1.h>
#include <zephyr/drivers/display/uc8159c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <string.h>

LOG_MODULE_REGISTER(display_six_rects, CONFIG_LOG_DEFAULT_LEVEL);

static const struct device *const display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static unsigned band_from_y(uint16_t y, uint16_t height)
{
	unsigned stripe = (height + 5U) / 6U;

	if (stripe == 0U) {
		stripe = 1U;
	}
	unsigned b = y / stripe;

	return (b > 5U) ? 5U : b;
}

static void fill_uc8159c(uint8_t *buf, uint16_t width, uint16_t height)
{
	/*
	 * Six horizontal bands, top → bottom. Values are DTM1 nibbles for GDEP073E01
	 * (Good Display / GxEPD2 native map in uc8159c.h), not Waveshare 7IN3 literals.
	 */
	static const uint8_t c[6] = {
		UC8159C_PALETTE_YELLOW, UC8159C_PALETTE_RED,   UC8159C_PALETTE_GREEN,
		UC8159C_PALETTE_BLUE,   UC8159C_PALETTE_BLACK, UC8159C_PALETTE_WHITE,
	};

	for (uint16_t y = 0U; y < height; y++) {
		unsigned b = band_from_y(y, height);

		for (uint16_t xb = 0U; xb < width / 2U; xb++) {
			buf[y * (width / 2U) + xb] = UC8159C_PACK_PIXELS(c[b], c[b]);
		}
	}
}

static void fill_l4(uint8_t *buf, uint16_t width, uint16_t height)
{
	static const uint8_t c[6] = {
		AC057TC1_COLOR_BLACK, AC057TC1_COLOR_WHITE,  AC057TC1_COLOR_GREEN,
		AC057TC1_COLOR_BLUE,  AC057TC1_COLOR_RED,    AC057TC1_COLOR_YELLOW,
	};

	for (uint16_t y = 0U; y < height; y++) {
		unsigned b = band_from_y(y, height);

		for (uint16_t xb = 0U; xb < width / 2U; xb++) {
			buf[y * (width / 2U) + xb] = AC057TC1_PACK_PIXELS(c[b], c[b]);
		}
	}
}

static void fill_rgb565(uint8_t *buf, uint16_t width, uint16_t height)
{
	static const uint16_t c[6] = {
		0xF800U, /* red */
		0x07E0U, /* green */
		0x001FU, /* blue */
		0xFFE0U, /* yellow */
		0x07FFU, /* cyan */
		0xF81FU, /* magenta */
	};

	uint16_t *row = (uint16_t *)buf;

	for (uint16_t y = 0U; y < height; y++) {
		unsigned b = band_from_y(y, height);
		uint16_t color = sys_cpu_to_le16(c[b]);

		for (uint16_t x = 0U; x < width; x++) {
			row[y * width + x] = color;
		}
	}
}

static void fill_mono10_msb(uint8_t *buf, uint16_t width, uint16_t height)
{
	static const uint8_t c[6] = { 0x00U, 0xFFU, 0xAAU, 0x55U, 0xCCU, 0x33U };
	const size_t row_bytes = width / 8U;

	for (uint16_t y = 0U; y < height; y++) {
		unsigned b = band_from_y(y, height);
		uint8_t bytepat = c[b];

		memset(&buf[y * row_bytes], bytepat, row_bytes);
	}
}

int main(void)
{
	struct display_capabilities caps;
	struct display_buffer_descriptor desc;
	void *buf = NULL;
	size_t buf_size = 0;
	int err;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display not ready");
		return 0;
	}

	display_get_capabilities(display_dev, &caps);

	err = display_set_pixel_format(display_dev, caps.current_pixel_format);
	if (err != 0) {
		LOG_ERR("display_set_pixel_format failed: %d", err);
		return 0;
	}

	switch (caps.current_pixel_format) {
	case PIXEL_FORMAT_UC8159C:
		if ((caps.x_resolution % 2U) != 0U) {
			LOG_ERR("Width must be even for UC8159C");
			return 0;
		}
		buf_size = UC8159C_BUFFER_SIZE(caps.x_resolution, caps.y_resolution);
		break;
	case PIXEL_FORMAT_L_4:
		if ((caps.x_resolution % 2U) != 0U) {
			LOG_ERR("Width must be even for L_4");
			return 0;
		}
		buf_size = AC057TC1_BUFFER_SIZE(caps.x_resolution, caps.y_resolution);
		break;
	case PIXEL_FORMAT_RGB_565:
		buf_size = (size_t)caps.x_resolution * caps.y_resolution * 2U;
		break;
	case PIXEL_FORMAT_MONO10:
		if ((caps.x_resolution % 8U) != 0U) {
			LOG_ERR("Width must be multiple of 8 for MONO10");
			return 0;
		}
		buf_size = (size_t)caps.x_resolution * caps.y_resolution / 8U;
		break;
	default:
		LOG_ERR("Unsupported pixel format %#x", caps.current_pixel_format);
		return 0;
	}

	buf = k_malloc(buf_size);
	if (buf == NULL) {
		LOG_ERR("k_malloc(%zu) failed; increase CONFIG_HEAP_MEM_POOL_SIZE", buf_size);
		return 0;
	}

	switch (caps.current_pixel_format) {
	case PIXEL_FORMAT_UC8159C:
		fill_uc8159c(buf, caps.x_resolution, caps.y_resolution);
		break;
	case PIXEL_FORMAT_L_4:
		fill_l4(buf, caps.x_resolution, caps.y_resolution);
		break;
	case PIXEL_FORMAT_RGB_565:
		fill_rgb565(buf, caps.x_resolution, caps.y_resolution);
		break;
	case PIXEL_FORMAT_MONO10:
		fill_mono10_msb(buf, caps.x_resolution, caps.y_resolution);
		break;
	default:
		break;
	}

	desc.buf_size = buf_size;
	desc.width = caps.x_resolution;
	desc.height = caps.y_resolution;
	desc.pitch = caps.x_resolution;
	desc.frame_incomplete = false;

	err = display_blanking_off(display_dev);
	if (err != 0) {
		LOG_WRN("display_blanking_off: %d", err);
	}

	err = display_write(display_dev, 0, 0, &desc, buf);
	if (err != 0) {
		LOG_ERR("display_write failed: %d", err);
	}

	k_free(buf);

	LOG_INF("display_six_rects: done (%u x %u, fmt %#x)", caps.x_resolution,
		caps.y_resolution, caps.current_pixel_format);

	return 0;
}
