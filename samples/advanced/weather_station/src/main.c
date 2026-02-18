/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_internal.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(weather_main, CONFIG_LOG_DEFAULT_LEVEL);

#if defined(CONFIG_SAMPLE_WEATHER_BT_NOTIFY_PERIOD_MS)
#define WEATHER_BT_NOTIFY_PERIOD_MS CONFIG_SAMPLE_WEATHER_BT_NOTIFY_PERIOD_MS
#else
#define WEATHER_BT_NOTIFY_PERIOD_MS 5000
#endif

int main(void)
{
	const struct device *display_dev;
	struct weather_model model;
	struct weather_ui ui;
	int64_t next_sensor_update;
	int64_t next_ui_update;
	int64_t next_ble_notify;
	int err;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	err = weather_model_init(&model);
	if (err != 0) {
		LOG_ERR("Model init failed: %d", err);
		return err;
	}

	err = weather_ui_init(&ui);
	if (err != 0) {
		LOG_ERR("UI init failed: %d", err);
		return err;
	}

	weather_ui_update(&ui, &model);

	err = display_blanking_off(display_dev);
	if (err < 0 && err != -ENOSYS) {
		LOG_ERR("Failed to disable display blanking: %d", err);
		return err;
	}

	if (IS_ENABLED(CONFIG_SAMPLE_WEATHER_ENABLE_BT)) {
		err = weather_ble_init(&model);
		if (err != 0) {
			LOG_WRN("Bluetooth init failed: %d", err);
		}
	}

	next_sensor_update = k_uptime_get();
	next_ui_update = next_sensor_update;
	next_ble_notify = next_sensor_update;

	while (1) {
		printk("main loop\n");
		int64_t now = k_uptime_get();
		int64_t next_deadline;
		uint32_t lvgl_wait;
		int64_t app_wait;
		uint32_t sleep_ms;

		if (now >= next_sensor_update) {
			(void)weather_model_update_sensor(&model);
			weather_model_update_time(&model);
			next_sensor_update = now + CONFIG_SAMPLE_WEATHER_SENSOR_PERIOD_MS;
		}

		if (now >= next_ui_update) {
			weather_model_update_time(&model);
			weather_ui_update(&ui, &model);
			next_ui_update = now + CONFIG_SAMPLE_WEATHER_UI_REFRESH_MS;
		}

		if (IS_ENABLED(CONFIG_SAMPLE_WEATHER_ENABLE_BT) && now >= next_ble_notify) {
			weather_ble_notify(&model);
			next_ble_notify = now + WEATHER_BT_NOTIFY_PERIOD_MS;
		}

		next_deadline = MIN(next_sensor_update, next_ui_update);
		if (IS_ENABLED(CONFIG_SAMPLE_WEATHER_ENABLE_BT)) {
			next_deadline = MIN(next_deadline, next_ble_notify);
		}

		lvgl_wait = lv_timer_handler();
		app_wait = next_deadline - k_uptime_get();
		if (app_wait < 1) {
			app_wait = 1;
		}

		sleep_ms = MIN((uint32_t)app_wait, MAX(lvgl_wait, 1U));
		k_msleep(sleep_ms);
	}
}
