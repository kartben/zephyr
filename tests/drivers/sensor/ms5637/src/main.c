/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "ms5637.h"

#define TIMESTAMP_NS 123456789012ULL

/* Allowed error of a decoded value, in millidegrees Celsius or Pa */
#define TOLERANCE_MILLI 1

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(ms5637));

static const struct sensor_chan_spec chan_press = {SENSOR_CHAN_PRESS, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};

/* Calibration and pressure conversion of the MS5637-02BA03 datasheet example */
static const struct ms5637_calibration cal = {
	.sens_t1 = 46372U,
	.off_t1 = 43981U,
	.tcs = 29059U,
	.tco = 27842U,
	.t_ref = 31553U,
	.tempsens = 28165U,
};

#define ADC_PRESSURE 6465444U

struct compensated {
	uint32_t adc_temperature;
	/* Expected temperature in centidegrees Celsius */
	int32_t temperature;
	/* Expected pressure in Pa */
	int32_t pressure;
};

static const struct compensated samples[] = {
	/* Datasheet example: 20 degrees Celsius, first order compensation only */
	{8077636U, 2000, 110002},
	/* Above 20 degrees Celsius */
	{8577636U, 3674, 114044},
	/* Below 20 degrees Celsius */
	{7477636U, -139, 104933},
	/* Below -15 degrees Celsius */
	{6477636U, -4264, 94358},
};

static struct ms5637_encoded_data edata;

static void fill_edata(uint32_t adc_temperature)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.base_timestamp_ns = TIMESTAMP_NS;
	edata.header.reading_count = 1U;
	edata.adc_pressure = ADC_PRESSURE;
	edata.adc_temperature = adc_temperature;
	edata.calibration = cal;
}

/* Thousandths of the channel unit */
static int64_t q31_to_milli(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000) >> (31 - shift);
}

static void check_reading(const struct sensor_q31_data *out, struct sensor_chan_spec chan,
			  const struct compensated *expected)
{
	int64_t actual;
	int64_t milli;

	zassert_equal(out->header.reading_count, 1);
	zassert_equal(out->header.base_timestamp_ns + out->readings[0].timestamp_delta,
		      TIMESTAMP_NS);
	zassert_equal(out->shift, 16);

	if (chan.chan_type == SENSOR_CHAN_PRESS) {
		/* Pa, from kPa */
		actual = q31_to_milli(out->readings[0].pressure, out->shift);
		milli = expected->pressure;
		/* Truncated toward zero */
		zassert_equal(out->readings[0].pressure,
			      (q31_t)(((int64_t)expected->pressure << 15) / 1000));
	} else {
		/* Millidegrees Celsius */
		actual = q31_to_milli(out->readings[0].temperature, out->shift);
		milli = (int64_t)expected->temperature * 10;
		zassert_equal(out->readings[0].temperature,
			      (q31_t)(((int64_t)expected->temperature << 15) / 100));
	}

	zassert_within(actual, milli, TOLERANCE_MILLI, "got %lld, expected %lld", actual, milli);
}

static void check_chan(struct sensor_chan_spec chan, const struct compensated *expected)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[3];
	} out;
	const uint16_t max_count = ARRAY_SIZE(out.extra) + 1U;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	/* One reading per call */
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out.data), 1);
	check_reading(&out.data, chan, expected);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out.data), 0);

	/* All readings in one call */
	fit = 0;
	memset(&out, 0, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out.data), 1);
	check_reading(&out.data, chan, expected);
	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out.data), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out.data), 0);
}

ZTEST(ms5637_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_press, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

ZTEST(ms5637_decoder, test_decode)
{
	for (size_t i = 0; i < ARRAY_SIZE(samples); i++) {
		TC_PRINT("ADC temperature %u\n", samples[i].adc_temperature);
		fill_edata(samples[i].adc_temperature);
		check_chan(chan_press, &samples[i]);
		check_chan(chan_temp, &samples[i]);
	}
}

ZTEST(ms5637_decoder, test_unsupported)
{
	const struct sensor_chan_spec chans[] = {
		{SENSOR_CHAN_ALL, 0},
		{SENSOR_CHAN_HUMIDITY, 0},
		{SENSOR_CHAN_PROX, 0},
		{SENSOR_CHAN_PRESS, 1},
		{SENSOR_CHAN_AMBIENT_TEMP, 1},
	};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit;

	fill_edata(samples[0].adc_temperature);

	for (size_t i = 0; i < ARRAY_SIZE(chans); i++) {
		fit = 0;
		zassert_equal(decoder->get_size_info(chans[i], &base_size, &frame_size), -ENOTSUP);
		zassert_equal(decoder->get_frame_count(buffer, chans[i], &frame_count), -ENOTSUP);
		zassert_equal(decoder->decode(buffer, chans[i], &fit, 1, &out), -ENOTSUP);
	}
}

ZTEST(ms5637_decoder, test_has_trigger)
{
	/* One-shot reads only: the decoder reports no trigger */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(ms5637_decoder, NULL, NULL, NULL, NULL, NULL);
