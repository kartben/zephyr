/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OVERLAY_H_
#define OVERLAY_H_

#include <stdbool.h>
#include <stdint.h>

/** An RGB565 frame to draw into */
struct overlay_frame {
	uint8_t *buf;
	uint16_t width;
	uint16_t height;
	/** Bytes per line */
	uint32_t pitch;
	/** True for RGB565X (big-endian) pixels */
	bool big_endian;
};

/** Height of a line of text at scale 1, in pixels */
#define OVERLAY_FONT_HEIGHT 7

/** Build an RGB565 color from 8-bit components */
#define OVERLAY_RGB(r, g, b) ((uint16_t)((((r) & 0xF8U) << 8) | (((g) & 0xFCU) << 3) | ((b) >> 3)))

void overlay_fill(const struct overlay_frame *frame, int x0, int y0, int x1, int y1,
		  uint16_t color);

void overlay_rect(const struct overlay_frame *frame, int x0, int y0, int x1, int y1, int thickness,
		  uint16_t color);

int overlay_text_width(const char *text, int scale);

void overlay_text(const struct overlay_frame *frame, int x, int y, const char *text, int scale,
		  uint16_t color);

#endif /* OVERLAY_H_ */
