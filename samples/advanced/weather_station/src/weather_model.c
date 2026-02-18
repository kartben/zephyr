/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_internal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/timeutil.h>
#include <zephyr/sys/util.h>

#define DHT0_NODE DT_ALIAS(dht0)

#if !DT_NODE_EXISTS(DHT0_NODE)
#error "weather_station sample requires a dht0 devicetree alias"
#endif

static const struct device *const weather_sensor = DEVICE_DT_GET(DHT0_NODE);

#if DT_NODE_EXISTS(DT_ALIAS(rtc))
#define WEATHER_HAS_RTC_ALIAS 1
static const struct device *const weather_rtc = DEVICE_DT_GET(DT_ALIAS(rtc));
#else
#define WEATHER_HAS_RTC_ALIAS 0
#endif

static int32_t weather_sensor_value_to_milli(const struct sensor_value *value)
{
	int64_t milli = (int64_t)value->val1 * 1000;

	milli += value->val2 / 1000;

	return (int32_t)milli;
}

static void format_milli_one_decimal(int32_t milli, char *buf, size_t len)
{
	int32_t tenths = milli / 100;
	int32_t abs_tenths = tenths < 0 ? -tenths : tenths;

	snprintk(buf, len, "%s%d.%d", tenths < 0 ? "-" : "", abs_tenths / 10,
		 abs_tenths % 10);
}

int weather_model_init(struct weather_model *model)
{
	if (model == NULL) {
		return -EINVAL;
	}

	memset(model, 0, sizeof(*model));

	model->sensor_dev = weather_sensor;
	if (!device_is_ready(model->sensor_dev)) {
		model->last_sensor_error = -ENODEV;
		return -ENODEV;
	}

#if WEATHER_HAS_RTC_ALIAS
	model->rtc_present = true;
	model->rtc_dev = weather_rtc;
	model->rtc_ready = device_is_ready(model->rtc_dev);
#else
	model->rtc_present = false;
	model->rtc_dev = NULL;
	model->rtc_ready = false;
#endif

	model->clock_source = WEATHER_CLOCK_SOURCE_UPTIME;
	model->last_sensor_error = -EAGAIN;
	model->last_rtc_error = model->rtc_ready ? -EAGAIN : -ENODEV;
	model->unix_ms_ref = 0;
	model->current_unix_ms = k_uptime_get();

	weather_model_update_time(model);
	weather_model_update_sensor(model);

	return 0;
}

int weather_model_update_sensor(struct weather_model *model)
{
	struct sensor_value temp;
	struct sensor_value humidity;
	int err;

	err = sensor_sample_fetch(model->sensor_dev);
	if (err == 0) {
		err = sensor_channel_get(model->sensor_dev, SENSOR_CHAN_AMBIENT_TEMP, &temp);
	}
	if (err == 0) {
		err = sensor_channel_get(model->sensor_dev, SENSOR_CHAN_HUMIDITY, &humidity);
	}

	if (err != 0) {
		model->last_sensor_error = err;
		return err;
	}

	model->temperature_milli_c = weather_sensor_value_to_milli(&temp);
	model->humidity_milli_pct = weather_sensor_value_to_milli(&humidity);
	model->measurements_valid = true;
	model->last_sample_ms = k_uptime_get();
	model->last_sensor_error = 0;

	return 0;
}

void weather_model_update_time(struct weather_model *model)
{
	model->uptime_ms = k_uptime_get();

	if (model->rtc_present && model->rtc_ready) {
		struct rtc_time rtc_now;
		int err;

		err = rtc_get_time(model->rtc_dev, &rtc_now);
		if (err == 0) {
			int64_t unix_s = timeutil_timegm64(rtc_time_to_tm(&rtc_now));

			model->rtc_time = rtc_now;
			model->clock_source = WEATHER_CLOCK_SOURCE_RTC;
			model->last_rtc_error = 0;

			if (unix_s != -1) {
				model->current_unix_ms =
					unix_s * MSEC_PER_SEC + (rtc_now.tm_nsec / NSEC_PER_MSEC);
				model->unix_ms_ref = model->current_unix_ms - model->uptime_ms;
			} else {
				model->current_unix_ms = model->unix_ms_ref + model->uptime_ms;
			}
			return;
		}

		model->last_rtc_error = err;
	}

	model->clock_source = WEATHER_CLOCK_SOURCE_UPTIME;
	model->current_unix_ms = model->unix_ms_ref + model->uptime_ms;
}

void weather_model_set_unix_reference(struct weather_model *model, int64_t unix_ms)
{
	model->unix_ms_ref = unix_ms - k_uptime_get();
	model->current_unix_ms = unix_ms;
}

int64_t weather_model_current_unix_ms(const struct weather_model *model)
{
	return model->current_unix_ms;
}

void weather_model_format_clock(const struct weather_model *model, char *buf, size_t len)
{
	if (model->clock_source == WEATHER_CLOCK_SOURCE_RTC) {
		snprintk(buf, len, "%04d-%02d-%02d %02d:%02d:%02d", model->rtc_time.tm_year + 1900,
			 model->rtc_time.tm_mon + 1, model->rtc_time.tm_mday, model->rtc_time.tm_hour,
			 model->rtc_time.tm_min, model->rtc_time.tm_sec);
		return;
	}

	uint64_t total_sec = model->uptime_ms / MSEC_PER_SEC;
	uint64_t hours = total_sec / 3600U;
	uint64_t minutes = (total_sec % 3600U) / 60U;
	uint64_t seconds = total_sec % 60U;

	snprintk(buf, len, "uptime %02llu:%02llu:%02llu", (unsigned long long)hours,
		 (unsigned long long)minutes, (unsigned long long)seconds);
}

void weather_model_format_temperature(const struct weather_model *model, char *buf, size_t len)
{
	int32_t milli;

	if (!model->measurements_valid) {
		snprintk(buf, len, "--.-");
		return;
	}

	milli = model->temperature_milli_c;
	if (IS_ENABLED(CONFIG_SAMPLE_WEATHER_USE_FAHRENHEIT)) {
		milli = (milli * 9) / 5 + 32000;
	}

	format_milli_one_decimal(milli, buf, len);
}

void weather_model_format_humidity(const struct weather_model *model, char *buf, size_t len)
{
	if (!model->measurements_valid) {
		snprintk(buf, len, "--.-");
		return;
	}

	format_milli_one_decimal(model->humidity_milli_pct, buf, len);
}

const char *weather_model_temperature_unit(void)
{
	return IS_ENABLED(CONFIG_SAMPLE_WEATHER_USE_FAHRENHEIT) ? "F" : "C";
}

const char *weather_model_clock_source_text(const struct weather_model *model)
{
	return model->clock_source == WEATHER_CLOCK_SOURCE_RTC ? "RTC" : "Uptime";
}
