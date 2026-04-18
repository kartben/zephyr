/*
 * Copyright (c) 2026 Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Image dithering library API
 */

#ifndef ZEPHYR_INCLUDE_DISPLAY_LIBDITHER_H_
#define ZEPHYR_INCLUDE_DISPLAY_LIBDITHER_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Image dithering library
 * @defgroup libdither_interface Dithering Library
 * @ingroup display_interface
 * @{
 */

/** Dithering algorithm selection */
enum libdither_algorithm {
	/** Simple threshold: pixels >= 128 become white, others black */
	LIBDITHER_THRESHOLD,
	/** Ordered dithering using a 4x4 Bayer matrix (input buffer not modified) */
	LIBDITHER_ORDERED,
	/** Floyd-Steinberg error diffusion (modifies the gray input buffer in-place) */
	LIBDITHER_FLOYD_STEINBERG,
};

/** Output pixel packing layout for the 1-bit monochrome buffer */
enum libdither_output_format {
	/**
	 * Horizontal (row-major) packing.
	 * Buffer size = ceil(width/8) * height bytes.
	 * Within each byte, bit 7 (MSB) is the leftmost pixel.
	 */
	LIBDITHER_HTILED,
	/**
	 * Vertical tiling, matching SCREEN_INFO_MONO_VTILED displays (e.g. SSD1306).
	 * Buffer size = width * ceil(height/8) bytes.
	 * Byte at [(y/8)*width + x], bit (y%8) represents pixel (x, y).
	 * Bit 0 (LSB) is the topmost pixel in each group of 8.
	 */
	LIBDITHER_VTILED,
};

/**
 * @brief Convert an 8-bit grayscale image to 1-bit monochrome using dithering.
 *
 * The @p mono buffer must be pre-allocated and zeroed by the caller before
 * calling this function, as the implementation only sets bits (never clears).
 *
 * For @ref LIBDITHER_FLOYD_STEINBERG, the @p gray buffer is modified in-place
 * during processing and should not be reused after the call.
 *
 * @param gray      8-bit grayscale input buffer, 1 byte per pixel, row-major.
 * @param mono      1-bit monochrome output buffer (pre-allocated and zeroed).
 * @param width     Image width in pixels.
 * @param height    Image height in pixels.
 * @param algorithm Dithering algorithm to apply.
 * @param output_fmt Output buffer pixel packing layout.
 *
 * @return 0 on success, -EINVAL on invalid parameters.
 */
int libdither_convert(uint8_t *gray, uint8_t *mono,
		      uint16_t width, uint16_t height,
		      enum libdither_algorithm algorithm,
		      enum libdither_output_format output_fmt);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DISPLAY_LIBDITHER_H_ */
