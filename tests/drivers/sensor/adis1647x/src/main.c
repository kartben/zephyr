/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adis1647x.h"

/* Allowed error of a decoded value, in micro-units of the channel */
#define TOLERANCE_MICRO 10

#define TEST_TS_NS 1234567890123ULL

#define ACCEL_SHIFT 9
#define GYRO_SHIFT  6
#define TEMP_SHIFT  12

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adis1647x));

enum test_kind {
	KIND_ACCEL,
	KIND_GYRO,
	KIND_TEMP,
};

struct test_chan {
	enum sensor_channel chan;
	enum test_kind kind;
	/* Index of the first value in the gyro, accel, temp sequence of the burst */
	uint8_t first;
	uint8_t num_values;
};

static const struct test_chan test_chans[] = {
	{SENSOR_CHAN_GYRO_X, KIND_GYRO, 0, 1},   {SENSOR_CHAN_GYRO_Y, KIND_GYRO, 1, 1},
	{SENSOR_CHAN_GYRO_Z, KIND_GYRO, 2, 1},   {SENSOR_CHAN_GYRO_XYZ, KIND_GYRO, 0, 3},
	{SENSOR_CHAN_ACCEL_X, KIND_ACCEL, 3, 1}, {SENSOR_CHAN_ACCEL_Y, KIND_ACCEL, 4, 1},
	{SENSOR_CHAN_ACCEL_Z, KIND_ACCEL, 5, 1}, {SENSOR_CHAN_ACCEL_XYZ, KIND_ACCEL, 3, 3},
	{SENSOR_CHAN_DIE_TEMP, KIND_TEMP, 6, 1},
};

static struct adis1647x_sample_data sample;

/* Raw values in burst order: x, y, z gyro, x, y, z accel, temperature */
static void fill_sample(uint8_t accel_scale_num, uint16_t gyro_scale_num, const int16_t raw[7])
{
	uint8_t *burst = (uint8_t *)&sample.burst_data;

	memset(&sample, 0xa5, sizeof(sample));
	sample.accel_scale_num = accel_scale_num;
	sample.gyro_scale_num = gyro_scale_num;
	sample.timestamp = TEST_TS_NS;

	sys_put_be16(0U, &burst[offsetof(struct adis1647x_burst_data, diag_stat)]);
	for (int i = 0; i < 7; i++) {
		sys_put_be16((uint16_t)raw[i],
			     &burst[offsetof(struct adis1647x_burst_data, x_gyro_out) + i * 2]);
	}
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Physical value of a raw sample in micro-m/s^2, micro-rad/s or micro-degC */
static int64_t raw_to_micro(enum test_kind kind, int16_t raw)
{
	switch (kind) {
	case KIND_ACCEL:
		/* accel_scale_num / 100 mg per LSB */
		return (int64_t)raw * sample.accel_scale_num * SENSOR_G / 100000;
	case KIND_GYRO:
		/* gyro_scale_num / 100000 dps per LSB, pi in nano-units */
		return (int64_t)raw * sample.gyro_scale_num * 3141592654LL / 18000000000LL;
	default:
		/* 0.1 degC per LSB */
		return (int64_t)raw * 100000;
	}
}

static int8_t kind_shift(enum test_kind kind)
{
	switch (kind) {
	case KIND_ACCEL:
		return ACCEL_SHIFT;
	case KIND_GYRO:
		return GYRO_SHIFT;
	default:
		return TEMP_SHIFT;
	}
}

static void check_value(enum test_kind kind, q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = raw_to_micro(kind, raw);
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_chan(const struct test_chan *tc, const int16_t raw[7], uint16_t max_count)
{
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_chan_spec chan = {tc->chan, 0};
	union {
		struct sensor_q31_data q31;
		struct sensor_three_axis_data xyz;
	} out;
	uint16_t frame_count;
	uint32_t fit = 0;
	int8_t shift;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	memset(&out, 0xff, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 1, "chan %d", tc->chan);
	zassert_equal(out.q31.header.reading_count, 1);
	zassert_equal(out.q31.header.base_timestamp_ns, TEST_TS_NS);
	zassert_equal(out.q31.shift, kind_shift(tc->kind), "chan %d", tc->chan);
	shift = out.q31.shift;

	if (tc->num_values == 3U) {
		zassert_equal(out.xyz.readings[0].timestamp_delta, 0);
		for (int i = 0; i < 3; i++) {
			check_value(tc->kind, out.xyz.readings[0].values[i], shift,
				    raw[tc->first + i]);
		}
	} else {
		zassert_equal(out.q31.readings[0].timestamp_delta, 0);
		check_value(tc->kind, out.q31.readings[0].value, shift, raw[tc->first]);
	}

	/* All readings have been decoded */
	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 0);
}

static void check_all_chans(const int16_t raw[7])
{
	for (size_t i = 0; i < ARRAY_SIZE(test_chans); i++) {
		/* One by one, and all readings in one call */
		check_chan(&test_chans[i], raw, 1);
		check_chan(&test_chans[i], raw, 8);
	}
}

/* Distinct accel and gyro scales of the ADIS16470, ADIS16475-1/2/3 and ADIS16477-1/2/3 */
static const struct {
	uint8_t accel_scale_num;
	uint16_t gyro_scale_num;
} model_scales[] = {
	{125, 10000}, {25, 625}, {25, 2500}, {25, 10000}, {125, 625}, {125, 2500},
};

ZTEST(adis1647x_decoder, test_models)
{
	static const int16_t raw[7] = {1000, -2000, 3, -4000, 800, -1, 250};

	for (size_t i = 0; i < ARRAY_SIZE(model_scales); i++) {
		fill_sample(model_scales[i].accel_scale_num, model_scales[i].gyro_scale_num, raw);
		check_all_chans(raw);
	}
}

ZTEST(adis1647x_decoder, test_full_range)
{
	static const int16_t raw_max[7] = {INT16_MAX, INT16_MAX, INT16_MAX, INT16_MAX,
					   INT16_MAX, INT16_MAX, INT16_MAX};
	static const int16_t raw_min[7] = {INT16_MIN, INT16_MIN, INT16_MIN, INT16_MIN,
					   INT16_MIN, INT16_MIN, INT16_MIN};

	/* Largest scales: 1.25 mg/LSB and 0.1 dps/LSB */
	fill_sample(125, 10000, raw_max);
	check_all_chans(raw_max);
	fill_sample(125, 10000, raw_min);
	check_all_chans(raw_min);

	/* Smallest scales: 0.25 mg/LSB and 0.00625 dps/LSB */
	fill_sample(25, 625, raw_max);
	check_all_chans(raw_max);
	fill_sample(25, 625, raw_min);
	check_all_chans(raw_min);
}

ZTEST(adis1647x_decoder, test_one_g)
{
	/* 1 g on Z is 800 LSB at 1.25 mg/LSB, 90 dps on X is 900 LSB at 0.1 dps/LSB */
	static const int16_t raw[7] = {900, 0, 0, 0, 0, 800, 250};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_q31_data out;
	uint32_t fit = 0;

	fill_sample(125, 10000, raw);

	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0},
				      &fit, 1, &out),
		      1);
	zassert_within(q31_to_micro(out.readings[0].value, out.shift), SENSOR_G, 1);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0},
				      &fit, 1, &out),
		      1);
	zassert_within(q31_to_micro(out.readings[0].value, out.shift), SENSOR_PI / 2, 2);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0},
				      &fit, 1, &out),
		      1);
	zassert_within(q31_to_micro(out.readings[0].value, out.shift), 25000000, 1);
}

ZTEST(adis1647x_decoder, test_max_count_zero)
{
	static const int16_t raw[7] = {1, 2, 3, 4, 5, 6, 7};
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill_sample(125, 625, raw);

	zassert_equal(decoder->decode((const uint8_t *)&sample,
				      (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0}, &fit, 0,
				      &out),
		      0);
	zassert_equal(fit, 0);
}

ZTEST(adis1647x_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	for (size_t i = 0; i < ARRAY_SIZE(test_chans); i++) {
		struct sensor_chan_spec chan = {test_chans[i].chan, 0};

		zassert_ok(decoder->get_size_info(chan, &base_size, &frame_size));
		if (test_chans[i].num_values == 3U) {
			zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
			zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
		} else {
			zassert_equal(base_size, sizeof(struct sensor_q31_data));
			zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
		}
	}
}

ZTEST(adis1647x_decoder, test_has_trigger)
{
	const uint8_t *buffer = (const uint8_t *)&sample;

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(adis1647x_decoder, test_unsupported)
{
	static const int16_t raw[7] = {1, 2, 3, 4, 5, 6, 7};

	fill_sample(125, 625, raw);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_PROX, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
}

ZTEST_SUITE(adis1647x_decoder, NULL, NULL, NULL, NULL, NULL);
