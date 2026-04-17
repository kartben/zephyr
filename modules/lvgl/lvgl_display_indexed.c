/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <lvgl.h>
#include "lvgl_display.h"

/* I4 palette: 16 entries × 4 bytes (lv_color32_t / ARGB8888) */
#define I4_PALETTE_HEADER_SIZE (16U * 4U)

void lvgl_flush_cb_indexed(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
	uint16_t w = area->x2 - area->x1 + 1;
	uint16_t h = area->y2 - area->y1 + 1;
	struct lvgl_disp_data *data = (struct lvgl_disp_data *)lv_display_get_user_data(display);
	const struct device *display_dev = data->display_dev;
	const bool is_epd = data->cap.screen_info & SCREEN_INFO_EPD;
	const bool is_last = lv_display_flush_is_last(display);

	/* Skip past the LVGL palette header to reach pixel data */
	px_map += I4_PALETTE_HEADER_SIZE;

	if (is_epd && !data->blanking_on && !is_last) {
		/*
		 * Turn on display blanking when using an EPD display.
		 * This prevents updates and the associated flicker if the
		 * screen is rendered in multiple steps.
		 */
		display_blanking_on(display_dev);
		data->blanking_on = true;
	}

	struct display_buffer_descriptor desc = {
		.buf_size = (w * h) / 2U,
		.width = w,
		.pitch = ROUND_UP(DIV_ROUND_UP(w, 2), LV_DRAW_BUF_STRIDE_ALIGN) * 2,
		.height = h,
		.frame_incomplete = !is_last,
	};

	display_write(display_dev, area->x1, area->y1, &desc, (void *)px_map);
	if (data->cap.screen_info & SCREEN_INFO_DOUBLE_BUFFER) {
		display_write(display_dev, area->x1, area->y1, &desc, (void *)px_map);
	}

	if (is_epd && is_last && data->blanking_on) {
		/*
		 * The entire screen has now been rendered. Update the
		 * display by disabling blanking.
		 */
		display_blanking_off(display_dev);
		data->blanking_on = false;
	}

	lv_display_flush_ready(display);
}
