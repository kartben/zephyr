/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

#include <zephyr/llext/llext.h>

EXPORT_SYMBOL(lv_label_create);
EXPORT_SYMBOL(lv_label_set_text);

EXPORT_SYMBOL(lv_img_create);
EXPORT_SYMBOL(lv_img_set_src);
EXPORT_SYMBOL(lv_obj_align);

EXPORT_SYMBOL(lv_bar_create);
EXPORT_SYMBOL(lv_bar_set_value);
EXPORT_SYMBOL(lv_obj_center);
EXPORT_SYMBOL(lv_anim_init);
EXPORT_SYMBOL(lv_anim_path_ease_in);
EXPORT_SYMBOL(lv_obj_set_size);
EXPORT_SYMBOL(lv_anim_start);

static uint32_t count;

lv_obj_t *win;

static void win_btn_close_event_handler(lv_event_t *e)
{
	lv_obj_del(win);
}

static void lv_btn_click_callback(lv_event_t *e)
{
	ARG_UNUSED(e);

	win = lv_win_create(lv_scr_act(), 40);
	lv_obj_t *btn;

	lv_win_add_title(win, "LLEXT FTW");
	btn = lv_win_add_btn(win, LV_SYMBOL_CLOSE, 60);
	lv_obj_add_event_cb(btn, win_btn_close_event_handler, LV_EVENT_CLICKED, NULL);

	lv_obj_t *cont = lv_win_get_content(win);

	struct llext *ext = llext_by_name("gui_ext");
	if (ext) {
		void (*fn)(lv_obj_t *);

		fn = llext_find_sym(&ext->exp_tab, "fill_window");
		if (fn == NULL) {
			printk("fill_window() not found in gui_ext\n");
			return -EINVAL;
		}
		fn(cont);
	} else {
		lv_obj_t *label = lv_label_create(cont);
		lv_label_set_text(label, "Load gui_ext first to see the magic!");
	}

	count = 0;
}

int main(void)
{
	char count_str[11] = {0};
	const struct device *display_dev;
	lv_obj_t *hello_world_label;
	lv_obj_t *count_label;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	if (IS_ENABLED(CONFIG_LV_Z_POINTER_KSCAN) || IS_ENABLED(CONFIG_LV_Z_POINTER_INPUT)) {
		lv_obj_t *hello_world_button;

		hello_world_button = lv_btn_create(lv_scr_act());
		lv_obj_align(hello_world_button, LV_ALIGN_CENTER, 0, -15);
		lv_obj_add_event_cb(hello_world_button, lv_btn_click_callback, LV_EVENT_CLICKED,
				    NULL);
		hello_world_label = lv_label_create(hello_world_button);
	} else {
		hello_world_label = lv_label_create(lv_scr_act());
	}

	lv_label_set_text(hello_world_label, "Hello world!");
	lv_obj_align(hello_world_label, LV_ALIGN_CENTER, 0, 0);

	count_label = lv_label_create(lv_scr_act());
	lv_obj_align(count_label, LV_ALIGN_BOTTOM_MID, 0, 0);

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		if ((count % 100) == 0U) {
			sprintf(count_str, "%d", count/100U);
			lv_label_set_text(count_label, count_str);
		}
		lv_task_handler();
		++count;
		k_sleep(K_MSEC(10));
	}
}
