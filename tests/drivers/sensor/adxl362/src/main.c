/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adxl362.h"

/* Allowed error of a decoded acceleration, in micro-m/s^2 */
#define TOLERANCE_UMS2 1000
/* Allowed error of a decoded temperature, in milli-degrees Celsius */
#define TOLERANCE_MC   1

#define SINGLE_TS_NS 123456789ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl362));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_axis[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const int32_t range_lsb_per_g[] = {
	[ADXL362_RANGE_2G] = ADXL362_ACCEL_2G_LSB_PER_G,
	[ADXL362_RANGE_4G] = ADXL362_ACCEL_4G_LSB_PER_G,
	[ADXL362_RANGE_8G] = ADXL362_ACCEL_8G_LSB_PER_G,
};

static const int8_t range_shift[] = {
	[ADXL362_RANGE_2G] = 5,
	[ADXL362_RANGE_4G] = 6,
	[ADXL362_RANGE_8G] = 7,
};

/* Large enough for any decode of this test */
#define OUT_MAX_READINGS 64

static union {
	struct sensor_three_axis_data xyz;
	struct sensor_q31_data q31;
	uint8_t buf[sizeof(struct sensor_three_axis_data) +
		    OUT_MAX_READINGS * sizeof(struct sensor_three_axis_sample_data)];
} out;

static int64_t q31_to_scaled(q31_t value, int8_t shift, int64_t unit)
{
	return ((int64_t)value * unit) >> (31 - shift);
}

static void check_accel(q31_t value, int8_t shift, int16_t raw, uint8_t range)
{
	int64_t expected = (int64_t)raw * SENSOR_G / range_lsb_per_g[range];
	int64_t actual = q31_to_scaled(value, shift, 1000000);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_temp(q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = (raw - ADXL362_TEMP_BIAS_LSB) * ADXL362_TEMP_MC_PER_LSB +
			   ADXL362_TEMP_BIAS_TEST_CONDITION * 1000;
	int64_t actual = q31_to_scaled(value, shift, 1000);

	zassert_within(actual, expected, TOLERANCE_MC, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_all_unsupported(const uint8_t *buffer)
{
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
}

ZTEST(adxl362_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (size_t i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_size_info(chan_axis[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

/* Registers XDATA_L to TEMP_H hold values sign-extended to 16 bits */
static void fill_sample(struct adxl362_sample_data *sample, uint8_t range, const int16_t xyz[3],
			int16_t temp)
{
	memset(sample, 0, sizeof(*sample));
	sample->selected_range = range;
	sample->timestamp = SINGLE_TS_NS;

	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)xyz[i], &sample->raw[i * 2]);
	}
	sys_put_le16((uint16_t)temp, &sample->raw[6]);
}

static void check_single(uint8_t range, const int16_t xyz[3], int16_t temp)
{
	struct adxl362_sample_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_sample(&sample, range, xyz, temp);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.xyz.header.reading_count, 1);
	zassert_equal(out.xyz.header.base_timestamp_ns, SINGLE_TS_NS);
	zassert_equal(out.xyz.readings[0].timestamp_delta, 0);
	zassert_equal(out.xyz.shift, range_shift[range]);
	for (int i = 0; i < 3; i++) {
		check_accel(out.xyz.readings[0].values[i], out.xyz.shift, xyz[i], range);
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	for (int i = 0; i < 3; i++) {
		fit = 0;
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
		zassert_equal(frame_count, 1);
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out), 1);
		zassert_equal(out.q31.header.base_timestamp_ns, SINGLE_TS_NS);
		zassert_equal(out.q31.shift, range_shift[range]);
		check_accel(out.q31.readings[0].value, out.q31.shift, xyz[i], range);
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out), 0);
	}

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
	zassert_equal(out.q31.header.reading_count, 1);
	zassert_equal(out.q31.header.base_timestamp_ns, SINGLE_TS_NS);
	zassert_equal(out.q31.shift, 8);
	check_temp(out.q31.readings[0].temperature, out.q31.shift, temp);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);
}

ZTEST(adxl362_decoder, test_single_2g)
{
	check_single(ADXL362_RANGE_2G, (const int16_t[]){1000, -2000, 2047}, 450);
}

ZTEST(adxl362_decoder, test_single_4g)
{
	check_single(ADXL362_RANGE_4G, (const int16_t[]){500, -1000, -2048}, -100);
}

ZTEST(adxl362_decoder, test_single_8g)
{
	check_single(ADXL362_RANGE_8G, (const int16_t[]){235, -470, 2000}, 2047);
}

ZTEST(adxl362_decoder, test_single_unsupported)
{
	struct adxl362_sample_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_sample(&sample, ADXL362_RANGE_2G, (const int16_t[]){1, 2, 3}, 0);
	check_all_unsupported(buffer);

	sample.selected_range = 3;
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

ZTEST(adxl362_decoder, test_single_has_trigger)
{
	struct adxl362_sample_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;

	/* XDATA_L has the bits of all STATUS flags reported as triggers */
	fill_sample(&sample, ADXL362_RANGE_4G, (const int16_t[]){0x0D, 0, 0}, 0);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

#ifdef CONFIG_ADXL362_STREAM

#define FIFO_MAX_FRAMES 60
#define LAST_TS_NS      10000000000ULL
#define STATUS_DRDY     BIT(0)
#define STATUS_FIFO_WTR BIT(2)
#define STATUS_FIFO_OVR BIT(3)

static struct {
	struct adxl362_fifo_data hdr;
	uint8_t data[FIFO_MAX_FRAMES * ADXL362_SAMPLE_SIZE];
} __packed fifo;

static int16_t fifo_value(int frame, uint8_t idx)
{
	switch (idx) {
	case 0:
		return frame * 30;
	case 1:
		return -frame * 30 - 1;
	case 2:
		return 1000 - frame;
	default:
		return 350 + frame * 10;
	}
}

static uint8_t *fifo_entry(int frame, uint8_t idx)
{
	size_t frame_size = fifo.hdr.has_tmp ? ADXL362_SAMPLE_SIZE : ADXL362_SAMPLE_SIZE_ACCEL;

	return &fifo.data[frame * frame_size + idx * 2];
}

static void put_entry(int frame, uint8_t tag, int16_t value)
{
	sys_put_le16(FIELD_PREP(ADXL362_FIFO_TAG_MSK, tag) | ((uint16_t)value & GENMASK(13, 0)),
		     fifo_entry(frame, tag));
}

static void fill_fifo(uint8_t odr, uint8_t range, bool has_tmp, int num_frames)
{
	uint8_t num_entries = has_tmp ? 4 : 3;

	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.has_tmp = has_tmp ? 1 : 0;
	fifo.hdr.selected_range = range;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.int_status = STATUS_FIFO_WTR;
	fifo.hdr.fifo_byte_count = num_frames * num_entries * 2;
	fifo.hdr.timestamp = LAST_TS_NS;

	for (int i = 0; i < num_frames; i++) {
		for (uint8_t idx = 0; idx < num_entries; idx++) {
			put_entry(i, idx, fifo_value(i, idx));
		}
	}
}

static uint64_t frame_ts(int frame, int num_frames, uint64_t period_ns)
{
	return LAST_TS_NS - (uint64_t)(num_frames - 1 - frame) * period_ns;
}

static void check_fifo_xyz(int num_frames, uint64_t period_ns)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const struct sensor_three_axis_sample_data *readings = out.xyz.readings;
	uint8_t range = fifo.hdr.selected_range;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, num_frames);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, num_frames, &out), num_frames);
	zassert_equal(out.xyz.header.reading_count, num_frames);
	zassert_equal(out.xyz.header.base_timestamp_ns, frame_ts(0, num_frames, period_ns));
	zassert_equal(out.xyz.shift, range_shift[range]);

	for (int i = 0; i < num_frames; i++) {
		zassert_equal(readings[i].timestamp_delta, i * period_ns);
		for (uint8_t idx = 0; idx < 3; idx++) {
			check_accel(readings[i].values[idx], out.xyz.shift, fifo_value(i, idx),
				    range);
		}
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, num_frames, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < num_frames; i++) {
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
		zassert_equal(out.xyz.header.base_timestamp_ns + readings[0].timestamp_delta,
			      frame_ts(i, num_frames, period_ns));
		for (uint8_t idx = 0; idx < 3; idx++) {
			check_accel(readings[0].values[idx], out.xyz.shift, fifo_value(i, idx),
				    range);
		}
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	/* Single axes */
	for (uint8_t idx = 0; idx < 3; idx++) {
		fit = 0;
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[idx], &frame_count));
		zassert_equal(frame_count, num_frames);
		zassert_equal(decoder->decode(buffer, chan_axis[idx], &fit, num_frames, &out),
			      num_frames);
		zassert_equal(out.q31.shift, range_shift[range]);
		for (int i = 0; i < num_frames; i++) {
			zassert_equal(out.q31.header.base_timestamp_ns +
					      out.q31.readings[i].timestamp_delta,
				      frame_ts(i, num_frames, period_ns));
			check_accel(out.q31.readings[i].value, out.q31.shift, fifo_value(i, idx),
				    range);
		}
	}
}

ZTEST(adxl362_decoder, test_fifo_temp)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const int num_frames = 4;
	const uint64_t period_ns = 10000000ULL;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL362_ODR_100_HZ, ADXL362_RANGE_4G, true, num_frames);

	check_fifo_xyz(num_frames, period_ns);

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, num_frames);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, num_frames, &out), num_frames);
	zassert_equal(out.q31.header.reading_count, num_frames);
	zassert_equal(out.q31.shift, 8);
	for (int i = 0; i < num_frames; i++) {
		zassert_equal(out.q31.header.base_timestamp_ns +
				      out.q31.readings[i].timestamp_delta,
			      frame_ts(i, num_frames, period_ns));
		check_temp(out.q31.readings[i].temperature, out.q31.shift, fifo_value(i, 3));
	}
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, num_frames, &out), 0);

	fit = 0;
	for (int i = 0; i < num_frames; i++) {
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
		zassert_equal(out.q31.header.base_timestamp_ns +
				      out.q31.readings[0].timestamp_delta,
			      frame_ts(i, num_frames, period_ns));
		check_temp(out.q31.readings[0].temperature, out.q31.shift, fifo_value(i, 3));
	}

	check_all_unsupported(buffer);
}

ZTEST(adxl362_decoder, test_fifo_no_temp)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL362_ODR_400_HZ, ADXL362_RANGE_8G, false, 3);

	check_fifo_xyz(3, 2500000ULL);

	zassert_equal(decoder->get_frame_count(buffer, chan_temp, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 3, &out), -ENOTSUP);
}

ZTEST(adxl362_decoder, test_fifo_2g)
{
	fill_fifo(ADXL362_ODR_25_HZ, ADXL362_RANGE_2G, true, 2);

	check_fifo_xyz(2, 40000000ULL);
}

ZTEST(adxl362_decoder, test_fifo_12_5hz)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const uint64_t period_ns = 80000000ULL;
	const struct sensor_three_axis_sample_data *readings = out.xyz.readings;
	uint16_t frame_count;
	uint32_t fit = 0;
	int decoded = 0;
	int rc;

	fill_fifo(ADXL362_ODR_12_5_HZ, ADXL362_RANGE_2G, false, FIFO_MAX_FRAMES);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, FIFO_MAX_FRAMES);

	/*
	 * The readings span more than UINT32_MAX ns: a call ends before a timestamp delta that
	 * does not fit, and the next one continues from there.
	 */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, FIFO_MAX_FRAMES, &out), 54);

	fit = 0;
	while ((rc = decoder->decode(buffer, chan_xyz, &fit, FIFO_MAX_FRAMES, &out)) > 0) {
		for (int i = 0; i < rc; i++) {
			zassert_equal(out.xyz.header.base_timestamp_ns +
					      readings[i].timestamp_delta,
				      frame_ts(decoded + i, FIFO_MAX_FRAMES, period_ns));
			check_accel(readings[i].x, out.xyz.shift, fifo_value(decoded + i, 0),
				    ADXL362_RANGE_2G);
		}
		decoded += rc;
	}
	zassert_equal(rc, 0);
	zassert_equal(decoded, FIFO_MAX_FRAMES);
}

ZTEST(adxl362_decoder, test_fifo_bad_tag)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const int num_frames = 3;
	const uint64_t period_ns = 5000000ULL;
	const struct sensor_three_axis_sample_data *readings = out.xyz.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL362_ODR_200_HZ, ADXL362_RANGE_2G, true, num_frames);
	/* Y entry of the second frame tagged as X */
	sys_put_le16(sys_get_le16(fifo_entry(1, 1)) & ~ADXL362_FIFO_TAG_MSK, fifo_entry(1, 1));

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, num_frames);

	/* The invalid reading is dropped, the others keep their timestamps */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, num_frames, &out), 2);
	zassert_equal(out.xyz.header.base_timestamp_ns, frame_ts(0, num_frames, period_ns));
	zassert_equal(readings[1].timestamp_delta, 2 * period_ns);
	check_accel(readings[1].y, out.xyz.shift, fifo_value(2, 1), ADXL362_RANGE_2G);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, num_frames, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_axis[1], &fit, num_frames, &out), 2);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_axis[0], &fit, num_frames, &out), num_frames);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, num_frames, &out), num_frames);
}

ZTEST(adxl362_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.int_status = STATUS_FIFO_OVR;
	fifo.hdr.timestamp = LAST_TS_NS;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

ZTEST(adxl362_decoder, test_fifo_has_trigger)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	fill_fifo(ADXL362_ODR_100_HZ, ADXL362_RANGE_2G, true, 1);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	fifo.hdr.int_status = STATUS_DRDY | STATUS_FIFO_OVR;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(adxl362_decoder, test_fifo_invalid)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL362_ODR_100_HZ, ADXL362_RANGE_2G, true, 1);
	fifo.hdr.accel_odr = 6;
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);

	fill_fifo(ADXL362_ODR_100_HZ, ADXL362_RANGE_2G, true, 1);
	fifo.hdr.selected_range = 3;
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

#endif /* CONFIG_ADXL362_STREAM */

ZTEST_SUITE(adxl362_decoder, NULL, NULL, NULL, NULL, NULL);
