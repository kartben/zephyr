/*
 * Copyright (c) 2026 Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/display/libdither.h>
#include <zephyr/sys/util.h>
#include <errno.h>

/* Set a single pixel in the output mono buffer. Only sets bits, never clears. */
static inline void set_pixel(uint8_t *mono, uint16_t x, uint16_t y,
			      uint16_t width,
			      enum libdither_output_format fmt,
			      int value)
{
	if (!value) {
		return;
	}

	if (fmt == LIBDITHER_VTILED) {
		/* byte: (y/8)*width + x, bit position: y%8 (LSB = topmost pixel) */
		mono[((uint32_t)(y / 8) * width) + x] |= BIT(y % 8);
	} else {
		/* byte: y * ceil(w/8) + x/8, bit position: 7-(x%8) (MSB = leftmost) */
		mono[(uint32_t)y * DIV_ROUND_UP(width, 8) + (x / 8)] |= BIT(7 - (x % 8));
	}
}

static void dither_threshold(uint8_t *gray, uint8_t *mono,
			      uint16_t width, uint16_t height,
			      enum libdither_output_format fmt)
{
	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			set_pixel(mono, x, y, width, fmt,
				  gray[(uint32_t)y * width + x] >= 128);
		}
	}
}

/* Standard 4x4 Bayer matrix, values scaled to 0-255 range */
static const uint8_t bayer4x4[4][4] = {
	{  0, 128,  32, 160 },
	{192,  64, 224,  96 },
	{ 48, 176,  16, 144 },
	{240, 112, 208,  80 },
};

static void dither_ordered(const uint8_t *gray, uint8_t *mono,
			    uint16_t width, uint16_t height,
			    enum libdither_output_format fmt)
{
	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			set_pixel(mono, x, y, width, fmt,
				  gray[(uint32_t)y * width + x] > bayer4x4[y & 3][x & 3]);
		}
	}
}

static void dither_floyd_steinberg(uint8_t *gray, uint8_t *mono,
				   uint16_t width, uint16_t height,
				   enum libdither_output_format fmt)
{
	for (uint16_t y = 0; y < height; y++) {
		for (uint16_t x = 0; x < width; x++) {
			uint32_t idx = (uint32_t)y * width + x;
			uint8_t old_val = gray[idx];
			uint8_t new_val = old_val >= 128 ? 255 : 0;
			int16_t error = (int16_t)old_val - (int16_t)new_val;

			set_pixel(mono, x, y, width, fmt, new_val);

			/* Distribute quantization error to neighboring pixels:
			 * right: 7/16, below-left: 3/16, below: 5/16, below-right: 1/16
			 */
			if (x + 1 < width) {
				gray[idx + 1] = (uint8_t)CLAMP(
					(int16_t)gray[idx + 1] + (error * 7) / 16, 0, 255);
			}
			if (y + 1 < height) {
				if (x > 0) {
					gray[idx + width - 1] = (uint8_t)CLAMP(
						(int16_t)gray[idx + width - 1] + (error * 3) / 16,
						0, 255);
				}
				gray[idx + width] = (uint8_t)CLAMP(
					(int16_t)gray[idx + width] + (error * 5) / 16, 0, 255);
				if (x + 1 < width) {
					gray[idx + width + 1] = (uint8_t)CLAMP(
						(int16_t)gray[idx + width + 1] + error / 16,
						0, 255);
				}
			}
		}
	}
}

int libdither_convert(uint8_t *gray, uint8_t *mono,
		      uint16_t width, uint16_t height,
		      enum libdither_algorithm algorithm,
		      enum libdither_output_format output_fmt)
{
	if (!gray || !mono || width == 0 || height == 0) {
		return -EINVAL;
	}

	switch (algorithm) {
	case LIBDITHER_THRESHOLD:
		dither_threshold(gray, mono, width, height, output_fmt);
		break;
	case LIBDITHER_ORDERED:
		dither_ordered(gray, mono, width, height, output_fmt);
		break;
	case LIBDITHER_FLOYD_STEINBERG:
		dither_floyd_steinberg(gray, mono, width, height, output_fmt);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
