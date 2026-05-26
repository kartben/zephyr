/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define DISPLAY_NODE DT_CHOSEN(zephyr_display)
#define DISPLAY_WIDTH DT_PROP(DISPLAY_NODE, width)
#define DISPLAY_HEIGHT DT_PROP(DISPLAY_NODE, height)
#define DISPLAY_PIXELS (DISPLAY_WIDTH * DISPLAY_HEIGHT)

static const struct device *dev = DEVICE_DT_GET(DISPLAY_NODE);

static const uint8_t expected_bayer_i4[] = {
	0x01, 0x01,
	0x10, 0x10,
	0x01, 0x01,
	0x10, 0x10,
};

static const uint8_t passthrough_i4[] = {
	0x01, 0x10,
	0x11, 0x00,
	0x10, 0x01,
	0x00, 0x11,
};

static void fill_rgb565_gray(uint8_t *buf)
{
	for (size_t i = 0; i < DISPLAY_PIXELS; i++) {
		sys_put_le16(0x8410, &buf[i * 2U]);
	}
}

static void display_dithering_before(void *fixture)
{
	ARG_UNUSED(fixture);

	zassert_true(device_is_ready(dev), "display device not ready");
	zassert_ok(display_blanking_off(dev), "display_blanking_off failed");
	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_I_4),
		   "display_set_pixel_format(I_4) failed");
	zassert_ok(display_clear(dev), "display_clear failed");
}

static void verify_dithered_output(enum display_pixel_format pixel_format, const void *buf,
				   size_t buf_size)
{
	uint8_t actual[sizeof(expected_bayer_i4)];
	struct display_buffer_descriptor write_desc = {
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
		.buf_size = buf_size,
	};
	struct display_buffer_descriptor read_desc = {
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
		.buf_size = sizeof(actual),
	};

	zassert_ok(display_set_pixel_format(dev, pixel_format),
		   "display_set_pixel_format(%u) failed", pixel_format);
	zassert_ok(display_write(dev, 0, 0, &write_desc, buf), "display_write failed");
	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_I_4),
		   "display_set_pixel_format(I_4) failed");
	zassert_ok(display_read(dev, 0, 0, &read_desc, actual), "display_read failed");
	zassert_mem_equal(expected_bayer_i4, actual, sizeof(actual));
}

ZTEST(display_dithering, test_capabilities_follow_claimed_format)
{
	struct display_capabilities caps;

	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_RGB_888),
		   "display_set_pixel_format(RGB_888) failed");
	display_get_capabilities(dev, &caps);
	zassert_equal(caps.x_resolution, DISPLAY_WIDTH);
	zassert_equal(caps.y_resolution, DISPLAY_HEIGHT);
	zassert_true((caps.supported_pixel_formats & PIXEL_FORMAT_I_4) != 0U);
	zassert_true((caps.supported_pixel_formats & PIXEL_FORMAT_RGB_565) != 0U);
	zassert_true((caps.supported_pixel_formats & PIXEL_FORMAT_RGB_888) != 0U);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_RGB_888);
	zassert_equal(caps.color_palette[0].r, 0U);
	zassert_equal(caps.color_palette[1].r, 255U);

	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_RGB_565),
		   "display_set_pixel_format(RGB_565) failed");
	display_get_capabilities(dev, &caps);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_RGB_565);

	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_I_4),
		   "display_set_pixel_format(I_4) failed");
	display_get_capabilities(dev, &caps);
	zassert_equal(caps.current_pixel_format, PIXEL_FORMAT_I_4);
}

ZTEST(display_dithering, test_rgb888_write_is_dithered_to_i4)
{
	uint8_t rgb888_gray[DISPLAY_PIXELS * 3U];

	memset(rgb888_gray, 0x80, sizeof(rgb888_gray));
	verify_dithered_output(PIXEL_FORMAT_RGB_888, rgb888_gray, sizeof(rgb888_gray));
}

ZTEST(display_dithering, test_rgb565_write_is_dithered_to_i4)
{
	uint8_t rgb565_gray[DISPLAY_PIXELS * 2U];

	fill_rgb565_gray(rgb565_gray);
	verify_dithered_output(PIXEL_FORMAT_RGB_565, rgb565_gray, sizeof(rgb565_gray));
}

ZTEST(display_dithering, test_i4_write_is_passthrough)
{
	uint8_t actual[sizeof(passthrough_i4)];
	struct display_buffer_descriptor desc = {
		.width = DISPLAY_WIDTH,
		.height = DISPLAY_HEIGHT,
		.pitch = DISPLAY_WIDTH,
		.buf_size = sizeof(passthrough_i4),
	};

	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_RGB_888),
		   "display_set_pixel_format(RGB_888) failed");
	zassert_ok(display_set_pixel_format(dev, PIXEL_FORMAT_I_4),
		   "display_set_pixel_format(I_4) failed");
	zassert_ok(display_write(dev, 0, 0, &desc, passthrough_i4), "display_write failed");
	zassert_ok(display_read(dev, 0, 0, &desc, actual), "display_read failed");
	zassert_mem_equal(passthrough_i4, actual, sizeof(actual));
}

ZTEST_SUITE(display_dithering, NULL, NULL, display_dithering_before, NULL, NULL);
