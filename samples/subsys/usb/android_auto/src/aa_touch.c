/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT android_auto_touch

#include "aa_display.h"
#include "aa_touch.h"

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

/*
 * Touch events from the head unit are injected as a touchscreen input device
 * that the LVGL pointer glue (zephyr,lvgl-pointer-input) consumes.
 */

int aa_touch_report(uint16_t x, uint16_t y, bool pressed)
{
	const struct device *dev = DEVICE_DT_INST_GET(0);
	const struct aa_display_geometry *g = aa_display_geometry();
	int ret;

	if (pressed) {
		uint32_t lx = MIN(x / g->scale, g->lv_width - 1U);
		uint32_t ly = MIN(y / g->scale, g->lv_height - 1U);

		ret = input_report_abs(dev, INPUT_ABS_X, (int32_t)lx, false, K_FOREVER);
		if (ret != 0) {
			return ret;
		}

		ret = input_report_abs(dev, INPUT_ABS_Y, (int32_t)ly, false, K_FOREVER);
		if (ret != 0) {
			return ret;
		}
	}

	return input_report_key(dev, INPUT_BTN_TOUCH, pressed ? 1 : 0, true, K_FOREVER);
}

int aa_touch_report_key(uint16_t code, bool pressed)
{
	const struct device *dev = DEVICE_DT_INST_GET(0);

	return input_report_key(dev, code, pressed ? 1 : 0, true, K_FOREVER);
}

DEVICE_DT_INST_DEFINE(0, NULL, NULL, NULL, NULL, POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);
