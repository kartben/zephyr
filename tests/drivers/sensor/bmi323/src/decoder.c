/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bmi323.h"
#include "bmi323_emul.h"

/* Allowed error of a decoded value, in micro-units of the channel */
#define TOLERANCE_MICRO 10

#define TIMESTAMP_NS 1234567890123ULL

/* Word order of the data registers */
enum {
	WORD_ACCEL_X,
	WORD_ACCEL_Y,
	WORD_ACCEL_Z,
	WORD_GYRO_X,
	WORD_GYRO_Y,
	WORD_GYRO_Z,
	WORD_TEMP,
	NUM_WORDS,
};

#define INVALID_SAMPLE ((int16_t)0x8000)

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bmi323_i2c));

static const struct sensor_chan_spec chan_accel_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_gyro_xyz = {SENSOR_CHAN_GYRO_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const struct sensor_chan_spec chan_accel[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
};

static const struct sensor_chan_spec chan_gyro[] = {
	{SENSOR_CHAN_GYRO_X, 0},
	{SENSOR_CHAN_GYRO_Y, 0},
	{SENSOR_CHAN_GYRO_Z, 0},
};

static struct bmi323_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static const int16_t test_words[NUM_WORDS] = {
	[WORD_ACCEL_X] = 16384,
	[WORD_ACCEL_Y] = -32767,
	[WORD_ACCEL_Z] = 1,
	[WORD_GYRO_X] = 32767,
	[WORD_GYRO_Y] = -1,
	[WORD_GYRO_Z] = -12345,
	[WORD_TEMP] = 512,
};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static int64_t accel_micro(int16_t raw, uint16_t range_g)
{
	return (int64_t)raw * range_g * SENSOR_G / 32768;
}

static int64_t gyro_micro(int16_t raw, uint16_t range_dps)
{
	return (int64_t)raw * range_dps * SENSOR_PI / (180 * 32768);
}

static int64_t temp_micro(int16_t raw)
{
	return (int64_t)raw * 1953 + 23000000;
}

static void check_value(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);
}

static void fill(const int16_t words[NUM_WORDS], uint16_t accel_range, uint16_t gyro_range)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.has_accel = true;
	edata.has_gyro = true;
	edata.has_temp = true;
	edata.accel_range = accel_range;
	edata.gyro_range = gyro_range;

	for (int i = 0; i < NUM_WORDS; i++) {
		sys_put_le16((uint16_t)words[i], &edata.data[i * sizeof(uint16_t)]);
	}
}

static void check_frame_count(struct sensor_chan_spec chan, uint16_t expected)
{
	uint16_t frame_count;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, expected, "channel %d: %u frames", chan.chan_type, frame_count);
}

/* Decode a three-axis channel with room for several readings, then one by one */
static void check_xyz(const uint8_t *buf, struct sensor_chan_spec chan, const int64_t expected[3],
		      uint64_t timestamp_ns)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[2];
	} out;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buf, chan, &fit, 3, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns, timestamp_ns);
	zassert_equal(out.data.readings[0].timestamp_delta, 0);
	zassert_equal(out.data.shift, 10);
	for (int i = 0; i < 3; i++) {
		check_value(out.data.readings[0].values[i], out.data.shift, expected[i]);
	}
	zassert_equal(decoder->decode(buf, chan, &fit, 3, &out), 0);

	fit = 0;
	memset(&out, 0, sizeof(out));
	zassert_equal(decoder->decode(buf, chan, &fit, 1, &out), 1);
	zassert_equal(out.data.header.base_timestamp_ns, timestamp_ns);
	for (int i = 0; i < 3; i++) {
		check_value(out.data.readings[0].values[i], out.data.shift, expected[i]);
	}
	zassert_equal(decoder->decode(buf, chan, &fit, 1, &out), 0);
}

static void check_single(const uint8_t *buf, struct sensor_chan_spec chan, int64_t expected,
			 uint64_t timestamp_ns)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buf, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, timestamp_ns);
	zassert_equal(out.shift, 10);
	check_value(out.readings[0].value, out.shift, expected);
	zassert_equal(decoder->decode(buf, chan, &fit, 1, &out), 0);
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

ZTEST(bmi323_decoder, test_size_info)
{
	static const struct sensor_chan_spec q31_chans[] = {
		{SENSOR_CHAN_ACCEL_X, 0}, {SENSOR_CHAN_ACCEL_Y, 0}, {SENSOR_CHAN_ACCEL_Z, 0},
		{SENSOR_CHAN_GYRO_X, 0},  {SENSOR_CHAN_GYRO_Y, 0},  {SENSOR_CHAN_GYRO_Z, 0},
		{SENSOR_CHAN_DIE_TEMP, 0},
	};
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_accel_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(chan_gyro_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	ARRAY_FOR_EACH(q31_chans, i) {
		zassert_ok(decoder->get_size_info(q31_chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(bmi323_decoder, test_accel)
{
	static const uint16_t ranges[] = {2, 4, 8, 16};

	ARRAY_FOR_EACH(ranges, r) {
		int64_t expected[3];

		fill(test_words, ranges[r], 2000);
		for (int i = 0; i < 3; i++) {
			expected[i] = accel_micro(test_words[WORD_ACCEL_X + i], ranges[r]);
		}

		check_frame_count(chan_accel_xyz, 1);
		check_xyz(buffer, chan_accel_xyz, expected, TIMESTAMP_NS);

		for (int i = 0; i < 3; i++) {
			check_frame_count(chan_accel[i], 1);
			check_single(buffer, chan_accel[i], expected[i], TIMESTAMP_NS);
		}
	}
}

ZTEST(bmi323_decoder, test_gyro)
{
	static const uint16_t ranges[] = {125, 250, 500, 1000, 2000};

	ARRAY_FOR_EACH(ranges, r) {
		int64_t expected[3];

		fill(test_words, 8, ranges[r]);
		for (int i = 0; i < 3; i++) {
			expected[i] = gyro_micro(test_words[WORD_GYRO_X + i], ranges[r]);
		}

		check_frame_count(chan_gyro_xyz, 1);
		check_xyz(buffer, chan_gyro_xyz, expected, TIMESTAMP_NS);

		for (int i = 0; i < 3; i++) {
			check_frame_count(chan_gyro[i], 1);
			check_single(buffer, chan_gyro[i], expected[i], TIMESTAMP_NS);
		}
	}
}

ZTEST(bmi323_decoder, test_temperature)
{
	static const int16_t raws[] = {0, 512, -1, -32767, 32767, -11776};
	int16_t words[NUM_WORDS];

	memcpy(words, test_words, sizeof(words));

	ARRAY_FOR_EACH(raws, i) {
		words[WORD_TEMP] = raws[i];
		fill(words, 8, 2000);

		check_frame_count(chan_temp, 1);
		check_single(buffer, chan_temp, temp_micro(raws[i]), TIMESTAMP_NS);
	}
}

ZTEST(bmi323_decoder, test_not_requested)
{
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint32_t fit = 0;

	/* A read of the temperature only */
	fill(test_words, 8, 2000);
	edata.has_accel = false;
	edata.has_gyro = false;

	check_frame_count(chan_accel_xyz, 0);
	check_frame_count(chan_accel[0], 0);
	check_frame_count(chan_gyro_xyz, 0);
	check_frame_count(chan_gyro[2], 0);
	check_frame_count(chan_temp, 1);

	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_gyro_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_accel[1], &fit, 1, &out_1), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 0, &out), -ENODATA);
	zassert_equal(fit, 0);
	check_single(buffer, chan_temp, temp_micro(test_words[WORD_TEMP]), TIMESTAMP_NS);

	edata.has_temp = false;
	edata.has_gyro = true;
	check_frame_count(chan_temp, 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out_1), -ENODATA);
	check_frame_count(chan_gyro_xyz, 1);
}

ZTEST(bmi323_decoder, test_invalid_sample)
{
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	int16_t words[NUM_WORDS];
	uint32_t fit = 0;

	memcpy(words, test_words, sizeof(words));
	words[WORD_ACCEL_Y] = INVALID_SAMPLE;
	words[WORD_TEMP] = INVALID_SAMPLE;
	fill(words, 4, 250);

	/* The reading is counted but dropped when decoded */
	check_frame_count(chan_accel_xyz, 1);
	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 1, &out), 0);
	zassert_equal(out.header.reading_count, 0);
	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 1, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_accel[1], &fit, 1, &out_1), 0);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out_1), 0);

	/* The valid axes are still decoded */
	check_single(buffer, chan_accel[0], accel_micro(words[WORD_ACCEL_X], 4), TIMESTAMP_NS);
	check_single(buffer, chan_accel[2], accel_micro(words[WORD_ACCEL_Z], 4), TIMESTAMP_NS);
}

ZTEST(bmi323_decoder, test_max_count_zero)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill(test_words, 8, 2000);

	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 0, &out), 0);
	/* Nothing was consumed */
	zassert_equal(decoder->decode(buffer, chan_accel_xyz, &fit, 1, &out), 1);
}

ZTEST(bmi323_decoder, test_has_trigger)
{
	fill(test_words, 8, 2000);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(bmi323_decoder, test_unsupported)
{
	fill(test_words, 8, 2000);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
}

/*
 * Read through the I2C emulator and decode the result. The SPI emulator only handles the
 * transfers of the sync API.
 */
SENSOR_DT_READ_IODEV(bmi323_i2c_iodev, DT_NODELABEL(bmi323_i2c), {SENSOR_CHAN_ALL, 0});
SENSOR_DT_READ_IODEV(bmi323_i2c_temp_iodev, DT_NODELABEL(bmi323_i2c), {SENSOR_CHAN_DIE_TEMP, 0});
SENSOR_DT_READ_IODEV(bmi323_i2c_motion_iodev, DT_NODELABEL(bmi323_i2c), {SENSOR_CHAN_ACCEL_Y, 0},
		     {SENSOR_CHAN_GYRO_XYZ, 0});
/* A completed read frees its SQE after posting its CQE: allow the next read to start before */
RTIO_DEFINE(bmi323_rtio_ctx, 2, 2);

static void check_read(const struct device *dev, const struct emul *target,
		       struct rtio_iodev *iodev)
{
	struct sensor_value range;
	uint8_t buf[sizeof(struct bmi323_encoded_data)] __aligned(8);
	const struct bmi323_encoded_data *read_data = (const struct bmi323_encoded_data *)buf;
	int64_t expected[3];
	uint64_t cycles;
	uint64_t before_ns;
	uint64_t after_ns;
	uint16_t accel_range;
	uint16_t gyro_range;
	int rc;

	zassert_true(device_is_ready(dev));

	zassert_ok(sensor_attr_get(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &range));
	accel_range = (uint16_t)range.val1;
	zassert_ok(sensor_attr_get(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &range));
	gyro_range = (uint16_t)range.val1;

	bmi323_emul_set_accel_raw(target, test_words[WORD_ACCEL_X], test_words[WORD_ACCEL_Y],
				  test_words[WORD_ACCEL_Z]);
	bmi323_emul_set_gyro_raw(target, test_words[WORD_GYRO_X], test_words[WORD_GYRO_Y],
				 test_words[WORD_GYRO_Z]);
	bmi323_emul_set_temperature_raw(target, test_words[WORD_TEMP]);

	zassert_ok(sensor_clock_get_cycles(&cycles));
	before_ns = sensor_clock_cycles_to_ns(cycles);
	rc = sensor_read(iodev, &bmi323_rtio_ctx, buf, sizeof(buf));
	zassert_ok(sensor_clock_get_cycles(&cycles));
	after_ns = sensor_clock_cycles_to_ns(cycles);

	/* Leave the emulator without valid samples, as after reset */
	bmi323_emul_set_accel_raw(target, INVALID_SAMPLE, INVALID_SAMPLE, INVALID_SAMPLE);
	bmi323_emul_set_gyro_raw(target, INVALID_SAMPLE, INVALID_SAMPLE, INVALID_SAMPLE);
	bmi323_emul_set_temperature_raw(target, INVALID_SAMPLE);

	zassert_ok(rc);
	zassert_true(read_data->header.timestamp >= before_ns &&
		     read_data->header.timestamp <= after_ns);

	for (int i = 0; i < 3; i++) {
		expected[i] = accel_micro(test_words[WORD_ACCEL_X + i], accel_range);
	}
	check_xyz(buf, chan_accel_xyz, expected, read_data->header.timestamp);

	for (int i = 0; i < 3; i++) {
		expected[i] = gyro_micro(test_words[WORD_GYRO_X + i], gyro_range);
	}
	check_xyz(buf, chan_gyro_xyz, expected, read_data->header.timestamp);

	check_single(buf, chan_temp, temp_micro(test_words[WORD_TEMP]),
		     read_data->header.timestamp);
}

ZTEST(bmi323_decoder, test_read_i2c)
{
	check_read(DEVICE_DT_GET(DT_NODELABEL(bmi323_i2c)), EMUL_DT_GET(DT_NODELABEL(bmi323_i2c)),
		   &bmi323_i2c_iodev);
}

/* Read a subset of the channels and check which ones the buffer holds */
static void read_partial(struct rtio_iodev *iodev, uint8_t *buf, size_t len)
{
	const struct emul *target = EMUL_DT_GET(DT_NODELABEL(bmi323_i2c));
	int rc;

	bmi323_emul_set_accel_raw(target, test_words[WORD_ACCEL_X], test_words[WORD_ACCEL_Y],
				  test_words[WORD_ACCEL_Z]);
	bmi323_emul_set_gyro_raw(target, test_words[WORD_GYRO_X], test_words[WORD_GYRO_Y],
				 test_words[WORD_GYRO_Z]);
	bmi323_emul_set_temperature_raw(target, test_words[WORD_TEMP]);

	rc = sensor_read(iodev, &bmi323_rtio_ctx, buf, len);

	bmi323_emul_set_accel_raw(target, INVALID_SAMPLE, INVALID_SAMPLE, INVALID_SAMPLE);
	bmi323_emul_set_gyro_raw(target, INVALID_SAMPLE, INVALID_SAMPLE, INVALID_SAMPLE);
	bmi323_emul_set_temperature_raw(target, INVALID_SAMPLE);

	zassert_ok(rc, "rc %d", rc);
}

ZTEST(bmi323_decoder, test_read_i2c_partial)
{
	uint8_t buf[sizeof(struct bmi323_encoded_data)] __aligned(8);
	const struct bmi323_encoded_data *read_data = (const struct bmi323_encoded_data *)buf;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	int64_t expected[3];
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_true(device_is_ready(DEVICE_DT_GET(DT_NODELABEL(bmi323_i2c))));

	read_partial(&bmi323_i2c_temp_iodev, buf, sizeof(buf));
	zassert_ok(decoder->get_frame_count(buf, chan_temp, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_ok(decoder->get_frame_count(buf, chan_accel_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_ok(decoder->get_frame_count(buf, chan_gyro[0], &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buf, chan_accel_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buf, chan_gyro_xyz, &fit, 1, &out), -ENODATA);
	check_single(buf, chan_temp, temp_micro(test_words[WORD_TEMP]),
		     read_data->header.timestamp);

	read_partial(&bmi323_i2c_motion_iodev, buf, sizeof(buf));
	zassert_ok(decoder->get_frame_count(buf, chan_temp, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_ok(decoder->get_frame_count(buf, chan_accel_xyz, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_ok(decoder->get_frame_count(buf, chan_accel[0], &frame_count));
	zassert_equal(frame_count, 1);
	zassert_ok(decoder->get_frame_count(buf, chan_gyro_xyz, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(buf, chan_temp, &fit, 1, &out_1), -ENODATA);
	check_single(buf, chan_accel[1],
		     accel_micro(test_words[WORD_ACCEL_Y], read_data->accel_range),
		     read_data->header.timestamp);
	for (int i = 0; i < 3; i++) {
		expected[i] = gyro_micro(test_words[WORD_GYRO_X + i], read_data->gyro_range);
	}
	check_xyz(buf, chan_gyro_xyz, expected, read_data->header.timestamp);
}

ZTEST_SUITE(bmi323_decoder, NULL, NULL, NULL, NULL, NULL);
