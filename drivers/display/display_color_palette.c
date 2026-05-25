/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_color_palette.h"

#include <string.h>

#include <zephyr/dt-bindings/display/panel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define DISPLAY_COLOR_PALETTE_DITHER_STEP 8

static const uint8_t display_color_palette_bayer4x4[4][4] = {
	{ 0,  8,  2, 10},
	{12,  4, 14,  6},
	{ 3, 11,  1,  9},
	{15,  7, 13,  5},
};

static uint8_t display_color_palette_dither_component(uint8_t component, uint32_t x, uint32_t y)
{
	int16_t offset;

	offset = ((int16_t)display_color_palette_bayer4x4[y & 0x3U][x & 0x3U] - 8) *
		 DISPLAY_COLOR_PALETTE_DITHER_STEP;

	return CLAMP((int16_t)component + offset, 0, UINT8_MAX);
}

static int display_color_palette_decode_rgb(const struct display_buffer_descriptor *desc,
					    enum display_pixel_format pixel_format,
					    const void *buf,
					    uint32_t x,
					    uint32_t y,
					    uint8_t *r,
					    uint8_t *g,
					    uint8_t *b)
{
	uint32_t pixel_idx = y * desc->pitch + x;
	const uint8_t *buf8 = buf;

	switch (pixel_format) {
	case PIXEL_FORMAT_RGB_565: {
		uint16_t pixel = sys_le16_to_cpu(((const uint16_t *)buf)[pixel_idx]);

		*r = ((pixel >> 11) & 0x1FU) * 255U / 31U;
		*g = ((pixel >> 5) & 0x3FU) * 255U / 63U;
		*b = (pixel & 0x1FU) * 255U / 31U;
		return 0;
	}
	case PIXEL_FORMAT_RGB_565X: {
		uint16_t pixel = sys_be16_to_cpu(((const uint16_t *)buf)[pixel_idx]);

		*r = ((pixel >> 11) & 0x1FU) * 255U / 31U;
		*g = ((pixel >> 5) & 0x3FU) * 255U / 63U;
		*b = (pixel & 0x1FU) * 255U / 31U;
		return 0;
	}
	case PIXEL_FORMAT_RGB_888:
		buf8 += pixel_idx * 3U;
		*b = buf8[0];
		*g = buf8[1];
		*r = buf8[2];
		return 0;
	default:
		return -ENOTSUP;
	}
}

static int display_color_palette_find_nearest_index(const uint32_t *palette, size_t palette_len,
						    uint8_t r, uint8_t g, uint8_t b)
{
	uint32_t best_distance = UINT32_MAX;
	int best_index = -1;

	for (size_t i = 0; i < palette_len; ++i) {
		uint32_t entry = palette[i];
		int16_t dr;
		int16_t dg;
		int16_t db;
		uint32_t distance;

		if (entry == PANEL_COLOR_PALETTE_NULL) {
			continue;
		}

		dr = (int16_t)r - (int16_t)((entry >> 16) & 0xFFU);
		dg = (int16_t)g - (int16_t)((entry >> 8) & 0xFFU);
		db = (int16_t)b - (int16_t)(entry & 0xFFU);
		distance = (uint32_t)(dr * dr) + (uint32_t)(dg * dg) + (uint32_t)(db * db);

		if (distance < best_distance) {
			best_distance = distance;
			best_index = (int)i;
			if (distance == 0U) {
				break;
			}
		}
	}

	return best_index;
}

int display_color_palette_convert_to_i4(const struct display_buffer_descriptor *desc,
					enum display_pixel_format pixel_format,
					const void *buf,
					const uint32_t *palette,
					size_t palette_len,
					uint8_t *converted_buf,
					size_t converted_buf_size)
{
	size_t required_size;
	size_t row_size;

	if ((desc == NULL) || (buf == NULL) || (palette == NULL) || (converted_buf == NULL)) {
		return -EINVAL;
	}

	if (desc->width > desc->pitch) {
		return -EINVAL;
	}

	row_size = DIV_ROUND_UP(desc->width, 2U);
	required_size = row_size * desc->height;
	if (converted_buf_size < required_size) {
		return -EINVAL;
	}

	if (DISPLAY_BITS_PER_PIXEL(pixel_format) == 0U) {
		return -ENOTSUP;
	}

	if (desc->buf_size <
	    (desc->pitch * desc->height * DISPLAY_BITS_PER_PIXEL(pixel_format)) / 8U) {
		return -EINVAL;
	}

	memset(converted_buf, 0, required_size);

	for (uint32_t y = 0U; y < desc->height; ++y) {
		for (uint32_t x = 0U; x < desc->width; ++x) {
			uint8_t r;
			uint8_t g;
			uint8_t b;
			int palette_idx;
			uint8_t dithered_r;
			uint8_t dithered_g;
			uint8_t dithered_b;
			size_t byte_idx = (y * row_size) + (x / 2U);

			if (display_color_palette_decode_rgb(desc, pixel_format, buf, x, y,
							    &r, &g, &b) != 0) {
				return -ENOTSUP;
			}

			dithered_r = display_color_palette_dither_component(r, x, y);
			dithered_g = display_color_palette_dither_component(g, x, y);
			dithered_b = display_color_palette_dither_component(b, x, y);
			palette_idx = display_color_palette_find_nearest_index(
				palette, palette_len, dithered_r, dithered_g, dithered_b);
			if (palette_idx < 0) {
				return -ENOTSUP;
			}

			if ((x & 0x1U) == 0U) {
				converted_buf[byte_idx] = (uint8_t)palette_idx << 4;
			} else {
				converted_buf[byte_idx] |= (uint8_t)palette_idx & 0x0FU;
			}
		}
	}

	return 0;
}
