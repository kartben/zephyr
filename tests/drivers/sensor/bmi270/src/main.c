/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bmi270.h"
#include "bmi270_decoder.h"

#ifdef CONFIG_BMI270_STREAM

/* Allowed relative error of a decoded value, in parts per million */
#define TOLERANCE_PPM 20

#define LAST_TS_NS 10000000000ULL

/* FIFO frame headers */
#define HDR_ACC_GYR    0x8CU
#define HDR_ACC        0x84U
#define HDR_GYR        0x88U
#define HDR_EMPTY      0x80U
#define HDR_SKIP       0x40U
#define HDR_SENSORTIME 0x44U
#define HDR_CONFIG_CHG 0x48U
/* INT tag bits of regular frame headers */
#define HDR_INT1_TAG   0x01U
#define HDR_INT2_TAG   0x02U

#define MAX_READINGS 8

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bmi270));

static const struct sensor_chan_spec chan_accel = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_gyro = {SENSOR_CHAN_GYRO_XYZ, 0};

static const uint16_t gyr_range_dps[] = {2000, 1000, 500, 250, 125};

static struct {
	struct bmi270_fifo_encoded_data edata;
	uint8_t fifo[256];
} __packed buf;

static const uint8_t *const buffer = (const uint8_t *)&buf;
static size_t fifo_len;

struct expected {
	int16_t raw[3];
	uint64_t timestamp_ns;
};

static void fifo_start(bool headerless, uint8_t acc_range, uint8_t gyr_range_idx, uint8_t acc_odr,
		       uint8_t gyr_odr)
{
	memset(&buf, 0, sizeof(buf));
	buf.edata.header.timestamp = LAST_TS_NS;
	buf.edata.header.is_fifo = 1;
	buf.edata.header.is_headerless = headerless ? 1 : 0;
	buf.edata.header.acc_range = acc_range;
	buf.edata.header.gyr_range_idx = gyr_range_idx;
	buf.edata.header.acc_odr = acc_odr;
	buf.edata.header.gyr_odr = gyr_odr;
	buf.edata.header.int_status = BMI270_INT_STATUS_1_FWM_INT;
	fifo_len = 0;
}

static void put_byte(uint8_t value)
{
	buf.fifo[fifo_len++] = value;
	buf.edata.fifo_byte_count = fifo_len;
}

static void put_xyz(const int16_t raw[3])
{
	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)raw[i], &buf.fifo[fifo_len]);
		fifo_len += 2;
	}
	buf.edata.fifo_byte_count = fifo_len;
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static int64_t accel_micro(int16_t raw, uint8_t acc_range)
{
	/* range_g * g / 32768 per LSB */
	return (int64_t)raw * (2 << acc_range) * SENSOR_G / 32768;
}

static int64_t gyro_micro(int16_t raw, uint8_t gyr_range_idx)
{
	/* range_dps * pi / 180 / 32768 rad/s per LSB */
	return (int64_t)raw * gyr_range_dps[gyr_range_idx] * SENSOR_PI / (180LL * 32768);
}

static void check_xyz(const struct sensor_three_axis_sample_data *reading, int8_t shift,
		      const int16_t raw[3], bool accel, uint8_t range)
{
	for (int i = 0; i < 3; i++) {
		int64_t expected = accel ? accel_micro(raw[i], range) : gyro_micro(raw[i], range);
		int64_t actual = q31_to_micro(reading->values[i], shift);
		int64_t tolerance = (llabs(expected) * TOLERANCE_PPM) / 1000000 + 1;

		zassert_within(actual, expected, tolerance, "raw %d: got %lld, expected %lld",
			       raw[i], actual, expected);
	}
}

static void check_count(struct sensor_chan_spec chan, uint16_t expected)
{
	uint16_t frame_count;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, expected, "chan %d: %u frames", chan.chan_type, frame_count);
}

/* Decode all readings of a channel in one call, then one by one */
static void check_decode(struct sensor_chan_spec chan, int8_t shift, uint8_t range,
			 const struct expected *exp, int count)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[MAX_READINGS - 1];
	} out;
	/* Not indexed through the one-element readings array of the header struct */
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	const bool accel = chan.chan_type == SENSOR_CHAN_ACCEL_XYZ;
	uint32_t fit = 0;

	zassert_true(count <= MAX_READINGS);
	check_count(chan, count);

	zassert_equal(decoder->decode(buffer, chan, &fit, MAX_READINGS, &out), count);
	if (count > 0) {
		zassert_equal(out.data.header.reading_count, count);
		zassert_equal(out.data.shift, shift);
	}

	for (int i = 0; i < count; i++) {
		const struct sensor_three_axis_sample_data *reading = &readings[i];

		zassert_equal(out.data.header.base_timestamp_ns + reading->timestamp_delta,
			      exp[i].timestamp_ns, "reading %d", i);
		check_xyz(reading, out.data.shift, exp[i].raw, accel, range);
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, MAX_READINGS, &out), 0);

	fit = 0;
	for (int i = 0; i < count; i++) {
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
		zassert_equal(out.data.header.reading_count, 1);
		zassert_equal(out.data.shift, shift);
		zassert_equal(out.data.header.base_timestamp_ns +
				      out.data.readings[0].timestamp_delta,
			      exp[i].timestamp_ns, "reading %d", i);
		check_xyz(&out.data.readings[0], out.data.shift, exp[i].raw, accel, range);
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

/* The buffer holds FIFO data, but no frame of the channel */
static void check_no_readings(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	check_count(chan, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
	zassert_equal(fit, 0);
}

ZTEST(bmi270_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_accel, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(chan_gyro, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
}

ZTEST(bmi270_decoder, test_unsupported)
{
	static const int16_t raw[3] = {1, 2, 3};

	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	put_byte(HDR_ACC_GYR);
	put_xyz(raw);
	put_xyz(raw);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0});
}

ZTEST(bmi270_decoder, test_not_fifo)
{
	const struct sensor_chan_spec accel_idx1 = {SENSOR_CHAN_ACCEL_XYZ, 1};
	const struct sensor_chan_spec die_temp = {SENSOR_CHAN_DIE_TEMP, 0};
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	buf.edata.header.is_fifo = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan_accel, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), -EINVAL);
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	/* The channel index is checked first, the channel type after the buffer type */
	zassert_equal(decoder->get_frame_count(buffer, accel_idx1, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, accel_idx1, &fit, 1, &out), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, die_temp, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, die_temp, &fit, 1, &out), -EINVAL);
}

ZTEST(bmi270_decoder, test_empty)
{
	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	check_decode(chan_accel, 5, 0, NULL, 0);
	check_decode(chan_gyro, 6, 0, NULL, 0);

	fifo_start(true, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	check_decode(chan_accel, 5, 0, NULL, 0);
	check_decode(chan_gyro, 6, 0, NULL, 0);
}

/* Header mode with control frames and frames carrying one sensor only */
ZTEST(bmi270_decoder, test_header_mode)
{
	/* 100 Hz accelerometer at +/-4 g, 200 Hz gyroscope at +/-500 dps */
	const uint64_t acc_period = 10000000ULL;
	const uint64_t gyr_period = 5000000ULL;
	const struct expected acc[] = {
		{{100, -200, 8192}, LAST_TS_NS - 2 * acc_period},
		{{-32768, 32767, 0}, LAST_TS_NS - acc_period},
		{{1, -1, -8192}, LAST_TS_NS},
	};
	const struct expected gyr[] = {
		{{10, 20, 30}, LAST_TS_NS - 3 * gyr_period},
		{{-32768, 0, 32767}, LAST_TS_NS - 2 * gyr_period},
		{{-5, 6, -7}, LAST_TS_NS - gyr_period},
		{{1000, -1000, 65}, LAST_TS_NS},
	};

	fifo_start(false, 1, 2, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_200_HZ);
	put_byte(HDR_SKIP);
	put_byte(0x01);
	put_byte(HDR_ACC_GYR | HDR_INT1_TAG);
	put_xyz(gyr[0].raw);
	put_xyz(acc[0].raw);
	put_byte(HDR_GYR | HDR_INT2_TAG);
	put_xyz(gyr[1].raw);
	put_byte(HDR_SENSORTIME);
	put_byte(0x11);
	put_byte(0x22);
	put_byte(0x33);
	put_byte(HDR_ACC | HDR_INT1_TAG | HDR_INT2_TAG);
	put_xyz(acc[1].raw);
	put_byte(HDR_CONFIG_CHG);
	put_byte(0x00);
	put_byte(0x00);
	put_byte(0x00);
	put_byte(0x00);
	put_byte(HDR_GYR);
	put_xyz(gyr[2].raw);
	put_byte(HDR_ACC_GYR);
	put_xyz(gyr[3].raw);
	put_xyz(acc[2].raw);
	/* Over-read marker at the end of the FIFO data */
	put_byte(HDR_EMPTY);
	put_byte(0x00);

	check_decode(chan_accel, 6, 1, acc, ARRAY_SIZE(acc));
	check_decode(chan_gyro, 6, 2, gyr, ARRAY_SIZE(gyr));
}

/* Header mode buffers without frames of one or both sensors */
ZTEST(bmi270_decoder, test_header_mode_no_readings)
{
	const struct expected acc[] = {
		{{1, 2, 3}, LAST_TS_NS},
	};

	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	put_byte(HDR_SKIP);
	put_byte(0x01);
	put_byte(HDR_EMPTY);
	put_byte(0x00);
	check_no_readings(chan_accel);
	check_no_readings(chan_gyro);

	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);
	put_byte(HDR_ACC);
	put_xyz(acc[0].raw);
	check_decode(chan_accel, 5, 0, acc, ARRAY_SIZE(acc));
	check_no_readings(chan_gyro);
}

/* A read capped to the buffer size can end in the middle of a frame */
ZTEST(bmi270_decoder, test_header_mode_truncated)
{
	const uint64_t period = 40000000ULL;
	const struct expected acc[] = {
		{{1, 2, 3}, LAST_TS_NS - period},
		{{4, 5, 6}, LAST_TS_NS},
	};
	const struct expected gyr[] = {
		{{7, 8, 9}, LAST_TS_NS - period},
		{{10, 11, 12}, LAST_TS_NS},
	};
	/* The truncated payload holds valid headers that must not be parsed */
	const int16_t truncated[3] = {HDR_ACC, HDR_ACC, HDR_ACC};

	fifo_start(false, 0, 0, BMI270_ACC_ODR_25_HZ, BMI270_GYR_ODR_25_HZ);
	for (size_t i = 0; i < ARRAY_SIZE(acc); i++) {
		put_byte(HDR_ACC_GYR);
		put_xyz(gyr[i].raw);
		put_xyz(acc[i].raw);
	}
	put_byte(HDR_ACC_GYR);
	put_xyz(truncated);
	put_xyz(truncated);
	buf.edata.fifo_byte_count -= 3;

	check_decode(chan_accel, 5, 0, acc, ARRAY_SIZE(acc));
	check_decode(chan_gyro, 6, 0, gyr, ARRAY_SIZE(gyr));
}

ZTEST(bmi270_decoder, test_headerless)
{
	/* 1600 Hz, accelerometer at +/-16 g, gyroscope at +/-125 dps */
	const uint64_t period = 625000ULL;
	const struct expected acc[] = {
		{{2048, -2048, 32767}, LAST_TS_NS - 2 * period},
		{{0, 1, -1}, LAST_TS_NS - period},
		{{-32768, 12345, -12345}, LAST_TS_NS},
	};
	const struct expected gyr[] = {
		{{262, -262, 0}, LAST_TS_NS - 2 * period},
		{{32767, -32768, 1}, LAST_TS_NS - period},
		{{-100, 200, -300}, LAST_TS_NS},
	};

	fifo_start(true, 3, 4, BMI270_ACC_ODR_1600_HZ, BMI270_GYR_ODR_1600_HZ);
	for (size_t i = 0; i < ARRAY_SIZE(acc); i++) {
		put_xyz(gyr[i].raw);
		put_xyz(acc[i].raw);
	}
	/* Trailing partial frame */
	put_byte(0x55);

	check_decode(chan_accel, 8, 3, acc, ARRAY_SIZE(acc));
	check_decode(chan_gyro, 6, 4, gyr, ARRAY_SIZE(gyr));
}

ZTEST(bmi270_decoder, test_ranges)
{
	const struct expected exp[] = {
		{{32767, -32768, 16384}, LAST_TS_NS - 40000000ULL},
		{{-1, 1, -16384}, LAST_TS_NS},
	};

	for (uint8_t range = 0; range < 4; range++) {
		fifo_start(false, range, 0, BMI270_ACC_ODR_25_HZ, BMI270_GYR_ODR_25_HZ);
		for (size_t i = 0; i < ARRAY_SIZE(exp); i++) {
			put_byte(HDR_ACC);
			put_xyz(exp[i].raw);
		}
		check_decode(chan_accel, 5 + range, range, exp, ARRAY_SIZE(exp));
	}

	for (uint8_t range = 0; range < ARRAY_SIZE(gyr_range_dps); range++) {
		fifo_start(true, 0, range, BMI270_ACC_ODR_25_HZ, BMI270_GYR_ODR_25_HZ);
		for (size_t i = 0; i < ARRAY_SIZE(exp); i++) {
			put_xyz(exp[i].raw);
			put_xyz(exp[i].raw);
		}
		check_decode(chan_gyro, 6, range, exp, ARRAY_SIZE(exp));
	}
}

ZTEST(bmi270_decoder, test_odr)
{
	static const struct {
		uint8_t odr;
		uint64_t period_ns;
	} odrs[] = {
		{BMI270_ACC_ODR_25D32_HZ, 1280000000ULL}, {BMI270_ACC_ODR_25D16_HZ, 640000000ULL},
		{BMI270_ACC_ODR_25D8_HZ, 320000000ULL},   {BMI270_ACC_ODR_25D4_HZ, 160000000ULL},
		{BMI270_ACC_ODR_25D2_HZ, 80000000ULL},    {BMI270_ACC_ODR_25_HZ, 40000000ULL},
		{BMI270_ACC_ODR_50_HZ, 20000000ULL},      {BMI270_ACC_ODR_100_HZ, 10000000ULL},
		{BMI270_ACC_ODR_200_HZ, 5000000ULL},      {BMI270_ACC_ODR_400_HZ, 2500000ULL},
		{BMI270_ACC_ODR_800_HZ, 1250000ULL},      {BMI270_ACC_ODR_1600_HZ, 625000ULL},
		{BMI270_GYR_ODR_3200_HZ, 312500ULL},
	};
	struct expected exp[] = {
		{{1, 2, 3}, 0},
		{{4, 5, 6}, LAST_TS_NS},
	};

	for (size_t i = 0; i < ARRAY_SIZE(odrs); i++) {
		exp[0].timestamp_ns = LAST_TS_NS - odrs[i].period_ns;

		fifo_start(false, 0, 0, odrs[i].odr, odrs[i].odr);
		for (size_t j = 0; j < ARRAY_SIZE(exp); j++) {
			put_byte(HDR_ACC_GYR);
			put_xyz(exp[j].raw);
			put_xyz(exp[j].raw);
		}
		check_decode(chan_accel, 5, 0, exp, ARRAY_SIZE(exp));
		check_decode(chan_gyro, 6, 0, exp, ARRAY_SIZE(exp));
	}

	/* A disabled sensor has no sample period: all readings get the buffer timestamp */
	exp[0].timestamp_ns = LAST_TS_NS;
	fifo_start(false, 0, 0, 0, 0);
	for (size_t j = 0; j < ARRAY_SIZE(exp); j++) {
		put_byte(HDR_ACC_GYR);
		put_xyz(exp[j].raw);
		put_xyz(exp[j].raw);
	}
	check_decode(chan_accel, 5, 0, exp, ARRAY_SIZE(exp));
	check_decode(chan_gyro, 6, 0, exp, ARRAY_SIZE(exp));
}

/* Readings spanning more than UINT32_MAX ns are decoded in several batches */
ZTEST(bmi270_decoder, test_slow_odr_batches)
{
	const uint64_t period = 1280000000ULL;
	const uint16_t batches[] = {4, 2};
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[MAX_READINGS - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	int16_t raw[6][3];
	uint32_t fit = 0;
	size_t idx = 0;

	fifo_start(false, 0, 0, BMI270_ACC_ODR_25D32_HZ, BMI270_GYR_ODR_25_HZ);
	for (size_t i = 0; i < ARRAY_SIZE(raw); i++) {
		const int16_t n = (int16_t)i;

		raw[i][0] = 100 * n;
		raw[i][1] = -100 * n;
		raw[i][2] = 1000 + n;
		put_byte(HDR_ACC);
		put_xyz(raw[i]);
	}
	check_count(chan_accel, ARRAY_SIZE(raw));

	for (size_t b = 0; b < ARRAY_SIZE(batches); b++) {
		zassert_equal(decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out),
			      batches[b], "batch %zu", b);
		zassert_equal(out.data.header.reading_count, batches[b]);
		zassert_equal(out.data.shift, 5);

		for (uint16_t i = 0; i < batches[b]; i++, idx++) {
			uint64_t ts =
				out.data.header.base_timestamp_ns + readings[i].timestamp_delta;

			zassert_equal(ts, LAST_TS_NS - (ARRAY_SIZE(raw) - 1 - idx) * period,
				      "reading %zu", idx);
			check_xyz(&readings[i], out.data.shift, raw[idx], true, 0);
		}
	}
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);
	check_no_readings(chan_gyro);
}

ZTEST(bmi270_decoder, test_has_trigger)
{
	fifo_start(false, 0, 0, BMI270_ACC_ODR_100_HZ, BMI270_GYR_ODR_100_HZ);

	buf.edata.header.int_status = BMI270_INT_STATUS_1_FWM_INT;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	buf.edata.header.int_status = BMI270_INT_STATUS_1_FFULL_INT;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	buf.edata.header.int_status = BMI270_INT_STATUS_1_FWM_INT | BMI270_INT_STATUS_1_FFULL_INT;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	buf.edata.header.int_status = BMI270_INT_STATUS_1_ACC_DRDY_INT;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

#else /* CONFIG_BMI270_STREAM */

/* Without FIFO streaming, reads use the generic submit path and its default decoder */
ZTEST(bmi270_decoder, test_default_decoder)
{
	const struct sensor_decoder_api *decoder;

	zassert_ok(sensor_get_decoder(DEVICE_DT_GET(DT_NODELABEL(bmi270)), &decoder));
	zassert_equal_ptr(decoder, &__sensor_default_decoder);
}

#endif /* CONFIG_BMI270_STREAM */

ZTEST_SUITE(bmi270_decoder, NULL, NULL, NULL, NULL, NULL);
