/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_DISPLAY_COLOR_PALETTE_H_
#define ZEPHYR_DRIVERS_DISPLAY_COLOR_PALETTE_H_

#include <zephyr/drivers/display.h>

#define DISPLAY_COLOR_PALETTE_EMULATED_PIXEL_FORMATS \
	(PIXEL_FORMAT_RGB_565 | PIXEL_FORMAT_RGB_565X | PIXEL_FORMAT_RGB_888)

int display_color_palette_convert_to_i4(const struct display_buffer_descriptor *desc,
					enum display_pixel_format pixel_format,
					const void *buf,
					const uint32_t *palette,
					size_t palette_len,
					uint8_t *converted_buf,
					size_t converted_buf_size);

#endif /* ZEPHYR_DRIVERS_DISPLAY_COLOR_PALETTE_H_ */
