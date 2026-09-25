/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adxl355.h"

/* Allowed error of a decoded value, in micro-m/s^2 */
#define TOLERANCE_UMS2 10

#define RAW_MAX 0x7ffff
#define RAW_MIN (-0x80000)

/* X axis marker of FIFO entries */
#define X_MARKER BIT(0)

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl355));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_axis[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
};

static int32_t range_lsb_per_g(enum adxl355_range range)
{
	switch (range) {
	case ADXL355_RANGE_2G:
		return ADXL355_SENSITIVITY_2G;
	case ADXL355_RANGE_4G:
		return ADXL355_SENSITIVITY_4G;
	default:
		return ADXL355_SENSITIVITY_8G;
	}
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_value(q31_t value, int8_t shift, int32_t raw, enum adxl355_range range)
{
	int64_t expected = (int64_t)raw * SENSOR_G / range_lsb_per_g(range);
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

/* Store a 20-bit sample left-justified in 3 big-endian bytes, as in the XDATA registers */
static void put_raw(uint8_t *dst, int32_t raw, uint8_t marker)
{
	sys_put_be24((((uint32_t)raw << 4) & GENMASK(23, 4)) | marker, dst);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
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

ZTEST(adxl355_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_size_info(chan_axis[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

static void check_single(enum adxl355_range range, const int32_t raw[3])
{
	struct adxl355_sample sample = {
		.range = range,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint64_t before_ns;
	uint64_t after_ns;
	uint32_t fit = 0;

	put_raw((uint8_t *)&sample.x, raw[0], 0);
	put_raw((uint8_t *)&sample.y, raw[1], 0);
	put_raw((uint8_t *)&sample.z, raw[2], 0);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	before_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	after_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	zassert_equal(out.header.reading_count, 1);
	zassert_between_inclusive(out.header.base_timestamp_ns, before_ns, after_ns);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 11);
	check_value(out.readings[0].x, out.shift, raw[0], range);
	check_value(out.readings[0].y, out.shift, raw[1], range);
	check_value(out.readings[0].z, out.shift, raw[2], range);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
		zassert_equal(frame_count, 1);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 1);
		zassert_equal(out_1.header.reading_count, 1);
		zassert_equal(out_1.shift, 11);
		check_value(out_1.readings[0].value, out_1.shift, raw[i], range);
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 0);
	}

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(adxl355_decoder, test_single_2g)
{
	/* 256000 LSB/g */
	check_single(ADXL355_RANGE_2G, (const int32_t[]){256000, -128000, 12345});
	check_single(ADXL355_RANGE_2G, (const int32_t[]){RAW_MAX, RAW_MIN, -1});
}

ZTEST(adxl355_decoder, test_single_4g)
{
	/* 128000 LSB/g */
	check_single(ADXL355_RANGE_4G, (const int32_t[]){128000, -256000, 1});
	check_single(ADXL355_RANGE_4G, (const int32_t[]){RAW_MAX, RAW_MIN, 0});
}

ZTEST(adxl355_decoder, test_single_8g)
{
	/* 64000 LSB/g */
	check_single(ADXL355_RANGE_8G, (const int32_t[]){64000, -64000, 448000});
	check_single(ADXL355_RANGE_8G, (const int32_t[]){RAW_MAX, RAW_MIN, -54321});
}

ZTEST(adxl355_decoder, test_single_invalid_range)
{
	struct adxl355_sample sample = {
		.range = 0,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

ZTEST(adxl355_decoder, test_unsupported)
{
	struct adxl355_sample sample = {
		.range = ADXL355_RANGE_2G,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 1});
}

#ifdef CONFIG_ADXL355_STREAM

#define MAX_FRAMES 4
#define LAST_TS_NS 10000000000ULL

static struct {
	struct adxl355_fifo_data hdr;
	/* Room for leading and trailing entries of incomplete frames */
	uint8_t entries[(MAX_FRAMES * 3 + 4) * 3];
} __packed fifo;

static int32_t frame_raw(int frame, int axis)
{
	static const int32_t base[] = {1000, -2000, 256000};

	return base[axis] + frame * 1111 * (axis + 1);
}

/*
 * Fill the FIFO buffer with lead entries of an incomplete frame, num_frames complete frames
 * and trail entries of another incomplete frame.
 */
static void fill_fifo(enum adxl355_odr odr, enum adxl355_range range, int lead, int num_frames,
		      int trail)
{
	int n = 0;

	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.timestamp = LAST_TS_NS;
	fifo.hdr.status1 = ADXL355_STATUS_FIFO_FULL_MSK;
	fifo.hdr.range = range;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.sample_set_size = 3;

	/* Y and Z entries of a frame read before */
	for (int i = 3 - lead; i < 3; i++) {
		put_raw(&fifo.entries[n++ * 3], -7, 0);
	}

	for (int f = 0; f < num_frames; f++) {
		for (int axis = 0; axis < 3; axis++) {
			put_raw(&fifo.entries[n++ * 3], frame_raw(f, axis),
				(axis == 0) ? X_MARKER : 0);
		}
	}

	for (int i = 0; i < trail; i++) {
		put_raw(&fifo.entries[n++ * 3], 7, (i == 0) ? X_MARKER : 0);
	}

	fifo.hdr.fifo_samples = n;
	fifo.hdr.fifo_byte_count = n * 3;
}

static void check_fifo(enum adxl355_range range, int num_frames, uint64_t period_ns)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[MAX_FRAMES - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	struct sensor_three_axis_data out_1;
	struct sensor_q31_data out_axis;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, num_frames);

	/* All frames in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_FRAMES, &out), num_frames);
	zassert_equal(out.data.header.reading_count, num_frames);
	zassert_equal(out.data.header.base_timestamp_ns, LAST_TS_NS - (num_frames - 1) * period_ns);
	zassert_equal(out.data.shift, 11);

	for (int f = 0; f < num_frames; f++) {
		zassert_equal(readings[f].timestamp_delta, f * period_ns);
		check_value(readings[f].x, out.data.shift, frame_raw(f, 0), range);
		check_value(readings[f].y, out.data.shift, frame_raw(f, 1), range);
		check_value(readings[f].z, out.data.shift, frame_raw(f, 2), range);
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_FRAMES, &out), 0);

	/* One frame per call */
	fit = 0;
	for (int f = 0; f < num_frames; f++) {
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out_1), 1);
		zassert_equal(out_1.header.base_timestamp_ns + out_1.readings[0].timestamp_delta,
			      LAST_TS_NS - (num_frames - 1 - f) * period_ns);
		check_value(out_1.readings[0].x, out_1.shift, frame_raw(f, 0), range);
		check_value(out_1.readings[0].y, out_1.shift, frame_raw(f, 1), range);
		check_value(out_1.readings[0].z, out_1.shift, frame_raw(f, 2), range);
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out_1), 0);

	/* Single axes */
	for (int axis = 0; axis < ARRAY_SIZE(chan_axis); axis++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[axis], &frame_count));
		zassert_equal(frame_count, num_frames);

		fit = 0;
		for (int f = 0; f < num_frames; f++) {
			zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_axis),
				      1);
			zassert_equal(out_axis.header.base_timestamp_ns +
					      out_axis.readings[0].timestamp_delta,
				      LAST_TS_NS - (num_frames - 1 - f) * period_ns);
			check_value(out_axis.readings[0].value, out_axis.shift, frame_raw(f, axis),
				    range);
		}

		zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_axis), 0);
	}
}

ZTEST(adxl355_decoder, test_fifo)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	fill_fifo(ADXL355_ODR_1000HZ, ADXL355_RANGE_2G, 0, 3, 0);
	check_fifo(ADXL355_RANGE_2G, 3, 1000000ULL);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	fifo.hdr.status1 = ADXL355_STATUS_DATA_RDY_MSK;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(adxl355_decoder, test_fifo_ranges)
{
	fill_fifo(ADXL355_ODR_4000HZ, ADXL355_RANGE_4G, 0, MAX_FRAMES, 0);
	check_fifo(ADXL355_RANGE_4G, MAX_FRAMES, 250000ULL);

	fill_fifo(ADXL355_ODR_4000HZ, ADXL355_RANGE_8G, 0, MAX_FRAMES, 0);
	check_fifo(ADXL355_RANGE_8G, MAX_FRAMES, 250000ULL);
}

ZTEST(adxl355_decoder, test_fifo_odr)
{
	/* The output data rate is 4000 Hz / 2^odr */
	for (int odr = ADXL355_ODR_4000HZ; odr <= ADXL355_ODR_3_906HZ; odr++) {
		fill_fifo(odr, ADXL355_RANGE_2G, 0, 3, 0);
		check_fifo(ADXL355_RANGE_2G, 3, 250000ULL << odr);
	}
}

ZTEST(adxl355_decoder, test_fifo_unaligned)
{
	/* The FIFO read starts at the Y or Z entry and ends within a frame */
	fill_fifo(ADXL355_ODR_500HZ, ADXL355_RANGE_2G, 2, 2, 2);
	check_fifo(ADXL355_RANGE_2G, 2, 2000000ULL);

	fill_fifo(ADXL355_ODR_500HZ, ADXL355_RANGE_2G, 1, 3, 1);
	check_fifo(ADXL355_RANGE_2G, 3, 2000000ULL);
}

ZTEST(adxl355_decoder, test_fifo_invalid)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL355_ODR_3_906HZ + 1, ADXL355_RANGE_2G, 0, 1, 0);
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);

	fill_fifo(ADXL355_ODR_4000HZ, 0, 0, 1, 0);
	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

ZTEST(adxl355_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0xff, sizeof(fifo));
	memset(&fifo.hdr, 0, sizeof(fifo.hdr));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.timestamp = LAST_TS_NS;
	fifo.hdr.status1 = ADXL355_STATUS_FIFO_FULL_MSK;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

#endif /* CONFIG_ADXL355_STREAM */

ZTEST_SUITE(adxl355_decoder, NULL, NULL, NULL, NULL, NULL);
