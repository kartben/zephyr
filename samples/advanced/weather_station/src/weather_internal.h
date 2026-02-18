/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef WEATHER_INTERNAL_H_
#define WEATHER_INTERNAL_H_

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/rtc.h>

enum weather_clock_source {
	WEATHER_CLOCK_SOURCE_RTC,
	WEATHER_CLOCK_SOURCE_UPTIME,
};

struct weather_model {
	const struct device *sensor_dev;
	const struct device *rtc_dev;
	bool rtc_present;
	bool rtc_ready;

	bool measurements_valid;
	int last_sensor_error;
	int last_rtc_error;
	int32_t temperature_milli_c;
	int32_t humidity_milli_pct;
	int64_t last_sample_ms;

	enum weather_clock_source clock_source;
	struct rtc_time rtc_time;
	int64_t uptime_ms;
	int64_t current_unix_ms;
	int64_t unix_ms_ref;
};

struct weather_ui {
	lv_obj_t *screen;
	lv_obj_t *header_card;
	lv_obj_t *temp_card;
	lv_obj_t *humidity_card;
	lv_obj_t *status_card;

	lv_obj_t *title_label;
	lv_obj_t *time_label;
	lv_obj_t *clock_source_label;
	lv_obj_t *temp_value_label;
	lv_obj_t *humidity_value_label;
	lv_obj_t *status_label;

	int16_t last_width;
	int16_t last_height;
};

int weather_model_init(struct weather_model *model);
int weather_model_update_sensor(struct weather_model *model);
void weather_model_update_time(struct weather_model *model);
void weather_model_set_unix_reference(struct weather_model *model, int64_t unix_ms);
int64_t weather_model_current_unix_ms(const struct weather_model *model);
void weather_model_format_clock(const struct weather_model *model, char *buf, size_t len);
void weather_model_format_temperature(const struct weather_model *model, char *buf, size_t len);
void weather_model_format_humidity(const struct weather_model *model, char *buf, size_t len);
const char *weather_model_temperature_unit(void);
const char *weather_model_clock_source_text(const struct weather_model *model);

int weather_ui_init(struct weather_ui *ui);
void weather_ui_update(struct weather_ui *ui, const struct weather_model *model);

int weather_ble_init(struct weather_model *model);
void weather_ble_notify(const struct weather_model *model);
const char *weather_ble_status_string(void);

#endif /* WEATHER_INTERNAL_H_ */
