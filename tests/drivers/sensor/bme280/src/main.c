/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "bme280.h"

#define TIMESTAMP_NS 123456789012ULL

/* Allowed error of a decoded value, in millionths of the channel unit */
#define TOLERANCE_TEMP  100  /* micro-degC, 1 LSB is 30.5 */
#define TOLERANCE_PRESS 4000 /* micro-kPa, 1 LSB is 3906 */
#define TOLERANCE_HUM   1    /* micro-%RH, exact */

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bme280));

static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};
static const struct sensor_chan_spec chan_press = {SENSOR_CHAN_PRESS, 0};
static const struct sensor_chan_spec chan_hum = {SENSOR_CHAN_HUMIDITY, 0};

static struct bme280_encoded_data edata;

static void fill(bool has_temp, bool has_press, bool has_humidity, int32_t comp_temp,
		 uint32_t comp_press, uint32_t comp_humidity)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.has_temp = has_temp ? 1U : 0U;
	edata.has_press = has_press ? 1U : 0U;
	edata.has_humidity = has_humidity ? 1U : 0U;
	edata.reading.comp_temp = comp_temp;
	edata.reading.comp_press = comp_press;
	edata.reading.comp_humidity = comp_humidity;
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/*
 * Decode one channel in one call and one reading at a time, check that both agree on the
 * value, shift and timestamp, and return the value in millionths of the channel unit.
 */
static int64_t decode_one(struct sensor_chan_spec chan, int8_t shift, q31_t *value)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[3];
	} out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	/* Garbage in the output must not show up in the timestamp */
	memset(&out, 0xaa, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 4, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.shift, shift);
	zassert_equal(out.data.header.base_timestamp_ns + out.data.readings[0].timestamp_delta,
		      TIMESTAMP_NS);
	zassert_equal(decoder->decode(buffer, chan, &fit, 4, &out), 0);

	fit = 0;
	memset(&out_1, 0xaa, sizeof(out_1));
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out_1), 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out_1), 1);
	zassert_equal(out_1.header.reading_count, 1);
	zassert_equal(out_1.shift, shift);
	zassert_equal(out_1.header.base_timestamp_ns + out_1.readings[0].timestamp_delta,
		      TIMESTAMP_NS);
	zassert_equal(out_1.readings[0].value, out.data.readings[0].value);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out_1), 0);

	*value = out.data.readings[0].value;

	return q31_to_micro(*value, shift);
}

static void check_temp(int32_t comp_temp)
{
	q31_t value;
	int64_t actual = decode_one(chan_temp, BME280_TEMP_SHIFT, &value);

	/* 0.01 degC per LSB */
	zassert_within(actual, (int64_t)comp_temp * 10000, TOLERANCE_TEMP, "%d: got %lld",
		       comp_temp, actual);
	/* The value is truncated toward zero */
	zassert_equal(value, (q31_t)((int64_t)comp_temp * 32768 / 100));
}

static void check_press(uint32_t comp_press)
{
	q31_t value;
	int64_t actual = decode_one(chan_press, BME280_PRESS_SHIFT, &value);

	/* UQ24.8 Pa, decoded in kPa */
	zassert_within(actual, (int64_t)comp_press * 1000 / 256, TOLERANCE_PRESS, "%u: got %lld",
		       comp_press, actual);
	zassert_equal(value, (q31_t)(comp_press / 1000U));
}

static void check_hum(uint32_t comp_humidity)
{
	q31_t value;
	int64_t actual = decode_one(chan_hum, BME280_HUM_SHIFT, &value);

	/* UQ22.10 %RH */
	zassert_within(actual, (int64_t)comp_humidity * 1000000 / 1024, TOLERANCE_HUM,
		       "%u: got %lld", comp_humidity, actual);
	zassert_equal(value, (q31_t)comp_humidity);
}

/* A supported channel that the buffer does not hold */
static void check_absent(struct sensor_chan_spec chan)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(bme280_decoder, test_size_info)
{
	const struct sensor_chan_spec chans[] = {chan_temp, chan_press, chan_hum};
	size_t base_size;
	size_t frame_size;

	ARRAY_FOR_EACH(chans, i) {
		zassert_ok(decoder->get_size_info(chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(bme280_decoder, test_bme280)
{
	/* 25.08 degC, 96386.199 Pa, 46.333 %RH */
	fill(true, true, true, 2508, 24674867, 47445);

	check_temp(2508);
	check_press(24674867);
	check_hum(47445);
}

ZTEST(bme280_decoder, test_limits)
{
	/* -40 degC, 30 kPa, 0 %RH */
	fill(true, true, true, -4000, 30000 * 256, 0);
	check_temp(-4000);
	check_press(30000 * 256);
	check_hum(0);

	/* 85 degC, 110 kPa, 100 %RH */
	fill(true, true, true, 8500, 110000 * 256 + 255, 100 * 1024);
	check_temp(8500);
	check_press(110000 * 256 + 255);
	check_hum(100 * 1024);

	/* Rounding toward zero below 0 degC */
	fill(true, true, true, -1, 0, 0);
	check_temp(-1);
	fill(true, true, true, -2507, 0, 0);
	check_temp(-2507);
}

ZTEST(bme280_decoder, test_bmp280)
{
	/* The BMP280 has no humidity sensor */
	fill(true, true, false, 2508, 24674867, 0);

	check_temp(2508);
	check_press(24674867);
	check_absent(chan_hum);
}

ZTEST(bme280_decoder, test_partial_read)
{
	/* Only the pressure was requested */
	fill(false, true, false, 2508, 24674867, 47445);

	check_absent(chan_temp);
	check_press(24674867);
	check_absent(chan_hum);
}

ZTEST(bme280_decoder, test_unsupported)
{
	fill(true, true, true, 2508, 24674867, 47445);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_PRESS, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_HUMIDITY, 1});
}

ZTEST(bme280_decoder, test_no_trigger)
{
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(bme280_decoder, NULL, NULL, NULL, NULL, NULL);
