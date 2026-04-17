/*
 * Copyright (c) 2019 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright 2023 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <errno.h>

#include "lvgl_display.h"

#ifdef CONFIG_LV_Z_FLUSH_THREAD

K_SEM_DEFINE(flush_complete, 0, 1);
/* Needed because the wait callback might be called even if not flush is pending */
K_SEM_DEFINE(flush_required, 0, 1);
/* Message queue will only ever need to queue one message */
K_MSGQ_DEFINE(flush_queue, sizeof(struct lvgl_display_flush), 1, 1);

void lvgl_flush_thread_entry(void *arg1, void *arg2, void *arg3)
{
	struct lvgl_display_flush flush;
	struct lvgl_disp_data *data;

	while (1) {
		k_msgq_get(&flush_queue, &flush, K_FOREVER);
		data = (struct lvgl_disp_data *)lv_display_get_user_data(flush.display);

		flush.desc.frame_incomplete = !lv_display_flush_is_last(flush.display);
		display_write(data->display_dev, flush.x, flush.y, &flush.desc, flush.buf);

		k_sem_give(&flush_complete);
	}
}

K_THREAD_DEFINE(lvgl_flush_thread, CONFIG_LV_Z_FLUSH_THREAD_STACK_SIZE, lvgl_flush_thread_entry,
		NULL, NULL, NULL, CONFIG_LV_Z_FLUSH_THREAD_PRIORITY, 0, 0);

void lvgl_wait_cb(lv_display_t *display)
{
	if (k_sem_take(&flush_required, K_NO_WAIT) == 0) {
		k_sem_take(&flush_complete, K_FOREVER);
	}
}

#endif /* CONFIG_LV_Z_FLUSH_THREAD */

#ifdef CONFIG_LV_Z_USE_ROUNDER_CB
void lvgl_rounder_cb(lv_event_t *e)
{
	lv_area_t *area = lv_event_get_param(e);
#if CONFIG_LV_Z_AREA_X_ALIGNMENT_WIDTH != 1
	__ASSERT(POPCOUNT(CONFIG_LV_Z_AREA_X_ALIGNMENT_WIDTH) == 1, "Invalid X alignment width");

	area->x1 &= ~(CONFIG_LV_Z_AREA_X_ALIGNMENT_WIDTH - 1);
	area->x2 |= (CONFIG_LV_Z_AREA_X_ALIGNMENT_WIDTH - 1);
#endif
#if CONFIG_LV_Z_AREA_Y_ALIGNMENT_WIDTH != 1
	__ASSERT(POPCOUNT(CONFIG_LV_Z_AREA_Y_ALIGNMENT_WIDTH) == 1, "Invalid Y alignment width");

	area->y1 &= ~(CONFIG_LV_Z_AREA_Y_ALIGNMENT_WIDTH - 1);
	area->y2 |= (CONFIG_LV_Z_AREA_Y_ALIGNMENT_WIDTH - 1);
#endif
}
#else
#define lvgl_rounder_cb NULL
#endif

int set_lvgl_rendering_cb(lv_display_t *display)
{
	int err = 0;
	struct lvgl_disp_data *data = (struct lvgl_disp_data *)lv_display_get_user_data(display);

#ifdef CONFIG_LV_Z_FLUSH_THREAD
	lv_display_set_flush_wait_cb(display, lvgl_wait_cb);
#endif

	switch (data->cap.current_pixel_format) {
	case PIXEL_FORMAT_ARGB_8888:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_ARGB8888);
		lv_display_set_flush_cb(display, lvgl_flush_cb_32bit);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_RGB_888:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
		lv_display_set_flush_cb(display, lvgl_flush_cb_24bit);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_RGB_565X:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
		lv_display_set_flush_cb(display, lvgl_flush_cb_16bit);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_L_8:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_L8);
		lv_display_set_flush_cb(display, lvgl_flush_cb_8bit);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_AL_88:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_AL88);
		lv_display_set_flush_cb(display, lvgl_flush_cb_16bit);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_I1);
		lv_display_set_flush_cb(display, lvgl_flush_cb_mono);
		lv_display_add_event_cb(display, lvgl_rounder_cb_mono, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	case PIXEL_FORMAT_I_4:
		lv_display_set_color_format(display, LV_COLOR_FORMAT_I4);
		lv_display_set_flush_cb(display, lvgl_flush_cb_indexed);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		break;
	default:
		lv_display_set_flush_cb(display, NULL);
		lv_display_add_event_cb(display, lvgl_rounder_cb, LV_EVENT_INVALIDATE_AREA,
					display);
		err = -ENOTSUP;
		break;
	}

	return err;
}

void lvgl_flush_display(struct lvgl_display_flush *request)
{
#ifdef CONFIG_LV_Z_FLUSH_THREAD
	/*
	 * LVGL will only start a flush once the previous one is complete,
	 * so we can reset the flush state semaphore here.
	 */
	k_sem_reset(&flush_complete);
	k_msgq_put(&flush_queue, request, K_FOREVER);
	k_sem_give(&flush_required);
	/* Explicitly yield, in case the calling thread is a cooperative one */
	k_yield();
#else
	/* Write directly to the display */
	struct lvgl_disp_data *data =
		(struct lvgl_disp_data *)lv_display_get_user_data(request->display);

	request->desc.frame_incomplete = !lv_display_flush_is_last(request->display);
	display_write(data->display_dev, request->x, request->y, &request->desc, request->buf);
	lv_display_flush_ready(request->display);
#endif
}

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(lvgl, CONFIG_LV_Z_LOG_LEVEL);

#define I4_PALETTE_SIZE 16

void lvgl_set_indexed_palette(lv_display_t *display, const struct device *dev)
{
	uint32_t palette[I4_PALETTE_SIZE] = {0};
	lv_draw_buf_t *draw_buf;
	int ret;

	ret = display_get_palette(dev, palette, I4_PALETTE_SIZE);
	if (ret < 0) {
		LOG_WRN("Display does not provide a palette (err %d), using default grayscale", ret);
		/* Fall back to evenly spaced grayscale */
		for (int i = 0; i < I4_PALETTE_SIZE; i++) {
			uint8_t gray = (i * 255) / (I4_PALETTE_SIZE - 1);

			palette[i] = 0xFF000000U | ((uint32_t)gray << 16) |
				     ((uint32_t)gray << 8) | gray;
		}
	}

	draw_buf = lv_display_get_buf_active(display);
	if (draw_buf == NULL) {
		LOG_ERR("No active draw buffer for palette setup");
		return;
	}

	for (int i = 0; i < I4_PALETTE_SIZE; i++) {
		lv_color32_t c32 = {
			.blue = (palette[i] >> 0) & 0xFF,
			.green = (palette[i] >> 8) & 0xFF,
			.red = (palette[i] >> 16) & 0xFF,
			.alpha = (palette[i] >> 24) & 0xFF,
		};

		lv_draw_buf_set_palette(draw_buf, i, c32);
	}
}
