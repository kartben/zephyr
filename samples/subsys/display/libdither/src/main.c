/*
 * Copyright (c) 2026 Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/libdither.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(libdither_sample, LOG_LEVEL_INF);

/*
 * Lookup table for reversing bit order within a byte.
 * Used when the display reports SCREEN_INFO_MONO_MSB_FIRST with VTILED layout,
 * which expects bit 7 (MSB) to represent the topmost pixel instead of bit 0.
 */
static const uint8_t bit_reverse_lut[256] = {
	0x00, 0x80, 0x40, 0xC0, 0x20, 0xA0, 0x60, 0xE0,
	0x10, 0x90, 0x50, 0xD0, 0x30, 0xB0, 0x70, 0xF0,
	0x08, 0x88, 0x48, 0xC8, 0x28, 0xA8, 0x68, 0xE8,
	0x18, 0x98, 0x58, 0xD8, 0x38, 0xB8, 0x78, 0xF8,
	0x04, 0x84, 0x44, 0xC4, 0x24, 0xA4, 0x64, 0xE4,
	0x14, 0x94, 0x54, 0xD4, 0x34, 0xB4, 0x74, 0xF4,
	0x0C, 0x8C, 0x4C, 0xCC, 0x2C, 0xAC, 0x6C, 0xEC,
	0x1C, 0x9C, 0x5C, 0xDC, 0x3C, 0xBC, 0x7C, 0xFC,
	0x02, 0x82, 0x42, 0xC2, 0x22, 0xA2, 0x62, 0xE2,
	0x12, 0x92, 0x52, 0xD2, 0x32, 0xB2, 0x72, 0xF2,
	0x0A, 0x8A, 0x4A, 0xCA, 0x2A, 0xAA, 0x6A, 0xEA,
	0x1A, 0x9A, 0x5A, 0xDA, 0x3A, 0xBA, 0x7A, 0xFA,
	0x06, 0x86, 0x46, 0xC6, 0x26, 0xA6, 0x66, 0xE6,
	0x16, 0x96, 0x56, 0xD6, 0x36, 0xB6, 0x76, 0xF6,
	0x0E, 0x8E, 0x4E, 0xCE, 0x2E, 0xAE, 0x6E, 0xEE,
	0x1E, 0x9E, 0x5E, 0xDE, 0x3E, 0xBE, 0x7E, 0xFE,
	0x01, 0x81, 0x41, 0xC1, 0x21, 0xA1, 0x61, 0xE1,
	0x11, 0x91, 0x51, 0xD1, 0x31, 0xB1, 0x71, 0xF1,
	0x09, 0x89, 0x49, 0xC9, 0x29, 0xA9, 0x69, 0xE9,
	0x19, 0x99, 0x59, 0xD9, 0x39, 0xB9, 0x79, 0xF9,
	0x05, 0x85, 0x45, 0xC5, 0x25, 0xA5, 0x65, 0xE5,
	0x15, 0x95, 0x55, 0xD5, 0x35, 0xB5, 0x75, 0xF5,
	0x0D, 0x8D, 0x4D, 0xCD, 0x2D, 0xAD, 0x6D, 0xED,
	0x1D, 0x9D, 0x5D, 0xDD, 0x3D, 0xBD, 0x7D, 0xFD,
	0x03, 0x83, 0x43, 0xC3, 0x23, 0xA3, 0x63, 0xE3,
	0x13, 0x93, 0x53, 0xD3, 0x33, 0xB3, 0x73, 0xF3,
	0x0B, 0x8B, 0x4B, 0xCB, 0x2B, 0xAB, 0x6B, 0xEB,
	0x1B, 0x9B, 0x5B, 0xDB, 0x3B, 0xBB, 0x7B, 0xFB,
	0x07, 0x87, 0x47, 0xC7, 0x27, 0xA7, 0x67, 0xE7,
	0x17, 0x97, 0x57, 0xD7, 0x37, 0xB7, 0x77, 0xF7,
	0x0F, 0x8F, 0x4F, 0xCF, 0x2F, 0xAF, 0x6F, 0xEF,
	0x1F, 0x9F, 0x5F, 0xDF, 0x3F, 0xBF, 0x7F, 0xFF,
};

int main(void)
{
	const struct device *dev;
	struct display_capabilities caps;
	struct display_buffer_descriptor buf_desc;
	uint8_t *gray = NULL;
	uint8_t *mono = NULL;
	size_t gray_size, mono_size;
	bool vtiled;
	enum libdither_output_format output_fmt;
	int ret;

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	/* Prefer MONO10 (black=1), fall back to MONO01 (white=1) */
	ret = display_set_pixel_format(dev, PIXEL_FORMAT_MONO10);
	if (ret != 0) {
		ret = display_set_pixel_format(dev, PIXEL_FORMAT_MONO01);
		if (ret != 0) {
			LOG_ERR("Could not set monochrome pixel format (%d)", ret);
			return ret;
		}
	}

	display_get_capabilities(dev, &caps);

	uint16_t width  = caps.x_resolution;
	uint16_t height = caps.y_resolution;

	LOG_INF("Display: %s %ux%u fmt=%u screen_info=0x%x",
		dev->name, width, height,
		caps.current_pixel_format, caps.screen_info);

	gray_size = (size_t)width * height;

	vtiled = (caps.screen_info & SCREEN_INFO_MONO_VTILED) != 0;
	mono_size = vtiled
		? (size_t)width * DIV_ROUND_UP(height, 8)
		: (size_t)DIV_ROUND_UP(width, 8) * height;

	gray = k_malloc(gray_size);
	mono = k_malloc(mono_size);
	if (!gray || !mono) {
		LOG_ERR("Failed to allocate buffers (%zu + %zu bytes)",
			gray_size, mono_size);
		ret = -ENOMEM;
		goto cleanup;
	}

	/* Diagonal gradient: top-left=black (0), bottom-right=white (255) */
	uint32_t denom = (uint32_t)(width - 1) + (height - 1);

	if (denom == 0) {
		denom = 1;
	}
	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			gray[(uint32_t)y * width + x] =
				(uint8_t)(((uint32_t)(x + y) * 255U) / denom);
		}
	}

	memset(mono, 0, mono_size);

	output_fmt = vtiled ? LIBDITHER_VTILED : LIBDITHER_HTILED;

	ret = libdither_convert(gray, mono, width, height,
				LIBDITHER_FLOYD_STEINBERG, output_fmt);
	if (ret != 0) {
		LOG_ERR("libdither_convert failed (%d)", ret);
		goto cleanup;
	}

	/* SCREEN_INFO_MONO_MSB_FIRST: bit 7 is the topmost pixel (not bit 0).
	 * Reverse bit order in each byte to match. */
	if (vtiled && (caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST)) {
		for (size_t i = 0; i < mono_size; i++) {
			mono[i] = bit_reverse_lut[mono[i]];
		}
	}

	/* PIXEL_FORMAT_MONO10: bit=1 means black, bit=0 means white.
	 * Invert the buffer since libdither produces bit=1 for white. */
	if (caps.current_pixel_format == PIXEL_FORMAT_MONO10) {
		for (size_t i = 0; i < mono_size; i++) {
			mono[i] ^= 0xFF;
		}
	}

	buf_desc.buf_size        = mono_size;
	buf_desc.width           = width;
	buf_desc.height          = height;
	buf_desc.pitch           = width;
	buf_desc.frame_incomplete = false;

	ret = display_write(dev, 0, 0, &buf_desc, mono);
	if (ret != 0) {
		LOG_ERR("display_write failed (%d)", ret);
		goto cleanup;
	}

	ret = display_blanking_off(dev);
	if (ret != 0 && ret != -ENOSYS) {
		LOG_ERR("display_blanking_off failed (%d)", ret);
		goto cleanup;
	}

	LOG_INF("Display sample done");
	ret = 0;

cleanup:
	k_free(gray);
	k_free(mono);
	return ret;
}
