/*
 * Copyright (c) 2023 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/sensor.h>

#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app, CONFIG_LOG_DEFAULT_LEVEL);

#define ACCEL_NODE DT_ALIAS(accel0)

static lv_obj_t *chart1;
static lv_chart_series_t *ser_x;
static lv_chart_series_t *ser_y;
static lv_chart_series_t *ser_z;
static lv_timer_t *sensor_timer;

static const struct device *const accel_sensor = DEVICE_DT_GET(ACCEL_NODE);

#ifdef CONFIG_SENSOR_ASYNC_API
SENSOR_DT_READ_IODEV(accel_read_iodev, ACCEL_NODE, {SENSOR_CHAN_ACCEL_XYZ, 0});
RTIO_DEFINE_WITH_MEMPOOL(accel_rtio, 4, 4, 16, 64, sizeof(void *));
#endif

#ifdef CONFIG_SAMPLE_ACCEL_STREAM
SENSOR_DT_STREAM_IODEV(accel_stream_iodev, ACCEL_NODE,
		       {SENSOR_TRIG_FIFO_WATERMARK, SENSOR_STREAM_DATA_INCLUDE});
static struct rtio_sqe *stream_handle;
#endif

static void chart_add_point(int32_t x, int32_t y, int32_t z)
{
	lv_chart_set_next_value(chart1, ser_x, x);
	lv_chart_set_next_value(chart1, ser_y, y);
	lv_chart_set_next_value(chart1, ser_z, z);
}

#ifdef CONFIG_SENSOR_ASYNC_API
/* Convert a Q31 value with the given shift to an integer number of m/s^2 */
static int32_t q31_to_int(q31_t value, int8_t shift)
{
	return (int32_t)(((int64_t)value << shift) / (INT64_C(1) << 31));
}

/* Decode every accelerometer frame of a sensor buffer into the chart, return the frame count */
static int chart_add_buffer(const uint8_t *buf)
{
	const struct sensor_decoder_api *decoder;
	const struct sensor_chan_spec ch = {SENSOR_CHAN_ACCEL_XYZ, 0};
	struct sensor_three_axis_data data;
	uint32_t fit = 0;
	uint16_t count = 0;
	int rc;

	rc = sensor_get_decoder(accel_sensor, &decoder);
	if (rc != 0) {
		return rc;
	}

	rc = decoder->get_frame_count(buf, ch, &count);
	if (rc != 0) {
		return rc;
	}

	for (uint16_t i = 0; i < count; i++) {
		if (decoder->decode(buf, ch, &fit, 1, &data) <= 0) {
			break;
		}
		chart_add_point(q31_to_int(data.readings[0].x, data.shift),
				q31_to_int(data.readings[0].y, data.shift),
				q31_to_int(data.readings[0].z, data.shift));
	}

	return count;
}

/* Timer handler: reads one sample and appends it to the chart */
static void sensor_timer_cb(lv_timer_t *timer)
{
	uint8_t buf[128];
	int rc = sensor_read(&accel_read_iodev, &accel_rtio, buf, sizeof(buf));

	if (rc == 0) {
		rc = chart_add_buffer(buf);
	}
	if (rc < 0) {
		LOG_ERR("Update failed: %d", rc);
	}
}
#else
/* Timer handler: fetches sensor data and appends it to the chart */
static void sensor_timer_cb(lv_timer_t *timer)
{
	struct sensor_value accel[3];
	int rc = sensor_sample_fetch(accel_sensor);

	if (rc == 0) {
		rc = sensor_channel_get(accel_sensor, SENSOR_CHAN_ACCEL_XYZ, accel);
	}
	if (rc < 0) {
		LOG_ERR("Update failed: %d", rc);
		return;
	}
	chart_add_point(sensor_value_to_double(&accel[0]), sensor_value_to_double(&accel[1]),
			sensor_value_to_double(&accel[2]));
}
#endif /* CONFIG_SENSOR_ASYNC_API */

static void set_sampling_rate(void)
{
	const struct sensor_value rate = {.val1 = CONFIG_SAMPLE_ACCEL_SAMPLING_RATE, .val2 = 0};
	int rc;

	rc = sensor_attr_set(accel_sensor, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
			     &rate);

	if (rc != 0 && rc != -ENOTSUP) {
		LOG_WRN("Could not set the sampling rate to %d Hz: %d",
			CONFIG_SAMPLE_ACCEL_SAMPLING_RATE, rc);
	}
}

static void start_polling(void)
{
	sensor_timer = lv_timer_create(sensor_timer_cb, 1000 / CONFIG_SAMPLE_ACCEL_SAMPLING_RATE,
				       NULL);
}

#ifdef CONFIG_SAMPLE_ACCEL_STREAM
/* Timer handler: appends every FIFO batch completed since the last call to the chart */
static void stream_timer_cb(lv_timer_t *timer)
{
	struct rtio_cqe *cqe;

	while ((cqe = rtio_cqe_consume(&accel_rtio)) != NULL) {
		int rc = cqe->result;
		uint8_t *buf;
		uint32_t buf_len;

		if (rc < 0) {
			rtio_cqe_release(&accel_rtio, cqe);
			if (rc == -ENOTSUP) {
				LOG_WRN("%s does not support streaming, polling instead",
					accel_sensor->name);
				rtio_sqe_cancel(stream_handle);
				lv_timer_delete(timer);
				start_polling();
				return;
			}
			LOG_ERR("Stream error: %d", rc);
			continue;
		}

		rc = rtio_cqe_get_mempool_buffer(&accel_rtio, cqe, &buf, &buf_len);
		rtio_cqe_release(&accel_rtio, cqe);
		if (rc != 0) {
			LOG_ERR("No buffer for the completed batch: %d", rc);
			continue;
		}

		rc = chart_add_buffer(buf);
		if (rc < 0) {
			LOG_ERR("Could not decode the batch: %d", rc);
		}
		rtio_release_buffer(&accel_rtio, buf, buf_len);
	}
}

static int start_streaming(void)
{
	const struct sensor_value batch = {
		.val1 = k_ms_to_cyc_near32(CONFIG_SAMPLE_ACCEL_STREAM_BATCH_MS),
		.val2 = 0,
	};
	int rc;

	rc = sensor_attr_set(accel_sensor, SENSOR_CHAN_ALL, SENSOR_ATTR_BATCH_DURATION, &batch);
	if (rc != 0) {
		LOG_WRN("Could not set the batch duration: %d", rc);
	}

	rc = sensor_stream(&accel_stream_iodev, &accel_rtio, NULL, &stream_handle);
	if (rc != 0) {
		LOG_WRN("Could not start streaming: %d", rc);
		return rc;
	}

	/* Poll the completion queue a few times per batch so that the chart follows closely */
	sensor_timer = lv_timer_create(stream_timer_cb,
				       MAX(CONFIG_SAMPLE_ACCEL_STREAM_BATCH_MS / 4, 5), NULL);
	LOG_INF("Streaming from %s, %d ms batches", accel_sensor->name,
		CONFIG_SAMPLE_ACCEL_STREAM_BATCH_MS);

	return 0;
}
#endif /* CONFIG_SAMPLE_ACCEL_STREAM */

static void create_accelerometer_chart(lv_obj_t *parent)
{
	chart1 = lv_chart_create(parent);
	lv_obj_set_size(chart1, LV_HOR_RES, LV_VER_RES);
	lv_chart_set_type(chart1, LV_CHART_TYPE_LINE);
	lv_chart_set_div_line_count(chart1, 5, 8);
	lv_chart_set_range(chart1, LV_CHART_AXIS_PRIMARY_Y, -20, 20); /* roughly -/+ 2G */
	lv_chart_set_update_mode(chart1, LV_CHART_UPDATE_MODE_SHIFT);

	ser_x = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_RED),
				    LV_CHART_AXIS_PRIMARY_Y);
	ser_y = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_BLUE),
				    LV_CHART_AXIS_PRIMARY_Y);
	ser_z = lv_chart_add_series(chart1, lv_palette_main(LV_PALETTE_GREEN),
				    LV_CHART_AXIS_PRIMARY_Y);

	lv_chart_set_point_count(chart1, CONFIG_SAMPLE_CHART_POINTS_PER_SERIES);

	/* Do not display point markers on the data */
	lv_obj_set_style_size(chart1, 0, 0, LV_PART_INDICATOR);
}

int main(void)
{
	const struct device *display_dev;
	int ret;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return -ENODEV;
	}

	if (!device_is_ready(accel_sensor)) {
		LOG_ERR("Device %s is not ready\n", accel_sensor->name);
		return -ENODEV;
	}

	create_accelerometer_chart(lv_screen_active());
	set_sampling_rate();
	if (!IS_ENABLED(CONFIG_SAMPLE_ACCEL_STREAM)) {
		start_polling();
	}
#ifdef CONFIG_SAMPLE_ACCEL_STREAM
	else if (start_streaming() != 0) {
		start_polling();
	}
#endif
	lv_timer_handler();
	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("Failed to turn blanking off (error %d)", ret);
		return 0;
	}

	while (1) {
		uint32_t sleep_ms = lv_timer_handler();

		k_msleep(MIN(sleep_ms, INT32_MAX));
	}

	return 0;
}
