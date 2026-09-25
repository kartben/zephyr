/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adxl372.h"

/* Allowed error of a decoded value, in micro-m/s^2 */
#define TOLERANCE_UMS2 1000

/* Sensitivity: 100 mg/LSB */
#define LSB_PER_G 10

#define SHIFT 11

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl372));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_axis[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_value(q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = (int64_t)raw * SENSOR_G / LSB_PER_G;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_not_null(decoder->get_size_info);
	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(adxl372_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_not_null(decoder->get_size_info);

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_size_info(chan_axis[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

ZTEST(adxl372_decoder, test_single)
{
	/* 12-bit samples left-justified in 16 bits, as adxl372_get_accel_data() stores them */
	static const int16_t raw[] = {2047, -2048, -1};
	struct adxl372_xyz_accel_data sample = {
		.x = (int16_t)(raw[0] * 16),
		.y = (int16_t)(raw[1] * 16),
		.z = (int16_t)(raw[2] * 16),
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, 0);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, SHIFT);
	check_value(out.readings[0].x, out.shift, raw[0]);
	check_value(out.readings[0].y, out.shift, raw[1]);
	check_value(out.readings[0].z, out.shift, raw[2]);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
		zassert_equal(frame_count, 1);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 1);
		zassert_equal(out_1.header.reading_count, 1);
		zassert_equal(out_1.shift, SHIFT);
		check_value(out_1.readings[0].value, out_1.shift, raw[i]);
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 0);
	}

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 1});
}

#ifdef CONFIG_ADXL372_STREAM

#define NUM_FRAMES     4
#define AXIS_SIZE      2
#define FRAME_SIZE_XYZ (3 * AXIS_SIZE)
#define LAST_TS_NS     1000000000ULL

static struct {
	struct adxl372_fifo_data hdr;
	uint8_t frames[NUM_FRAMES * FRAME_SIZE_XYZ];
} __packed fifo;

static int16_t frame_raw(int frame, int axis)
{
	static const int16_t raw[NUM_FRAMES][3] = {
		{100, -200, 2047},
		{700, -800, 682},
		{1300, -1400, -683},
		{1900, -2000, -2048},
	};

	return raw[frame][axis];
}

/* Big-endian 12-bit sample, left-justified; bit 0 marks the start of a series */
static void put_axis(uint8_t *dst, int16_t raw, bool first)
{
	sys_put_be16((uint16_t)(raw * 16) | (first ? 1U : 0U), dst);
}

/* Fill the FIFO with the axes set in axes, BIT(0) for X to BIT(2) for Z */
static void fill_fifo(enum adxl372_odr odr, uint8_t axes)
{
	uint8_t *dst = fifo.frames;
	uint8_t size = 0;

	memset(&fifo, 0, sizeof(fifo));

	for (int axis = 0; axis < 3; axis++) {
		if ((axes & BIT(axis)) != 0) {
			size += AXIS_SIZE;
		}
	}

	fifo.hdr.is_fifo = 1;
	fifo.hdr.sample_set_size = size;
	fifo.hdr.has_x = (axes & BIT(0)) != 0;
	fifo.hdr.has_y = (axes & BIT(1)) != 0;
	fifo.hdr.has_z = (axes & BIT(2)) != 0;
	fifo.hdr.int_status = ADXL372_INT1_MAP_FIFO_FULL_MSK;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.fifo_byte_count = NUM_FRAMES * size;
	fifo.hdr.timestamp = LAST_TS_NS;

	for (int i = 0; i < NUM_FRAMES; i++) {
		for (int axis = 0; axis < 3; axis++) {
			if ((axes & BIT(axis)) != 0) {
				put_axis(dst, frame_raw(i, axis), i == 0);
				dst += AXIS_SIZE;
			}
		}
	}
}

static uint64_t frame_ts(int frame, uint64_t period_ns)
{
	return LAST_TS_NS - (NUM_FRAMES - 1 - frame) * period_ns;
}

ZTEST(adxl372_decoder, test_fifo)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const uint64_t period_ns = 2500000;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[NUM_FRAMES - 1];
	} out_1;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	const struct sensor_q31_sample_data *readings_1 = out_1.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL372_ODR_400HZ, BIT_MASK(3));

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES);
	zassert_equal(out.data.header.base_timestamp_ns, frame_ts(0, period_ns));
	zassert_equal(out.data.shift, SHIFT);

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(readings[i].timestamp_delta, i * period_ns);
		check_value(readings[i].x, out.data.shift, frame_raw(i, 0));
		check_value(readings[i].y, out.data.shift, frame_raw(i, 1));
		check_value(readings[i].z, out.data.shift, frame_raw(i, 2));
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_FRAMES, &out), 0);

	for (int axis = 0; axis < ARRAY_SIZE(chan_axis); axis++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[axis], &frame_count));
		zassert_equal(frame_count, NUM_FRAMES);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, NUM_FRAMES, &out_1),
			      NUM_FRAMES);
		zassert_equal(out_1.data.header.base_timestamp_ns, frame_ts(0, period_ns));
		zassert_equal(out_1.data.shift, SHIFT);

		for (int i = 0; i < NUM_FRAMES; i++) {
			zassert_equal(readings_1[i].timestamp_delta, i * period_ns);
			check_value(readings_1[i].value, SHIFT, frame_raw(i, axis));
		}

		zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, NUM_FRAMES, &out_1),
			      0);
	}

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	fifo.hdr.int_status = ADXL372_INT1_MAP_DATA_RDY_MSK;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(adxl372_decoder, test_fifo_one_by_one)
{
	static const struct {
		enum adxl372_odr odr;
		uint64_t period_ns;
	} odrs[] = {
		{ADXL372_ODR_400HZ, 2500000}, {ADXL372_ODR_800HZ, 1250000},
		{ADXL372_ODR_1600HZ, 625000}, {ADXL372_ODR_3200HZ, 312500},
		{ADXL372_ODR_6400HZ, 156250},
	};
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;

	for (int o = 0; o < ARRAY_SIZE(odrs); o++) {
		uint32_t fit = 0;

		fill_fifo(odrs[o].odr, BIT_MASK(3));

		for (int i = 0; i < NUM_FRAMES; i++) {
			zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
			zassert_equal(out.header.reading_count, 1);
			zassert_equal(out.header.base_timestamp_ns +
					      out.readings[0].timestamp_delta,
				      frame_ts(i, odrs[o].period_ns));
			check_value(out.readings[0].x, out.shift, frame_raw(i, 0));
			check_value(out.readings[0].y, out.shift, frame_raw(i, 1));
			check_value(out.readings[0].z, out.shift, frame_raw(i, 2));
		}
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

		fit = 0;
		for (int i = 0; i < NUM_FRAMES; i++) {
			zassert_equal(decoder->decode(buffer, chan_axis[2], &fit, 1, &out_1), 1);
			zassert_equal(out_1.header.base_timestamp_ns +
					      out_1.readings[0].timestamp_delta,
				      frame_ts(i, odrs[o].period_ns));
			check_value(out_1.readings[0].value, out_1.shift, frame_raw(i, 2));
		}
		zassert_equal(decoder->decode(buffer, chan_axis[2], &fit, 1, &out_1), 0);
	}
}

ZTEST(adxl372_decoder, test_fifo_axes)
{
	/* X, Y, Z (2-byte frames), XY, XZ, YZ (4-byte frames) */
	static const uint8_t formats[] = {
		BIT(0), BIT(1), BIT(2), BIT(0) | BIT(1), BIT(0) | BIT(2), BIT(1) | BIT(2),
	};
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const uint64_t period_ns = 312500;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit;

	for (int f = 0; f < ARRAY_SIZE(formats); f++) {
		const uint8_t axes = formats[f];

		fill_fifo(ADXL372_ODR_3200HZ, axes);

		zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
		zassert_equal(frame_count, NUM_FRAMES);

		/* Axes missing from the frames read as 0 in every reading */
		memset(&out, 0x55, sizeof(out));
		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_FRAMES, &out),
			      NUM_FRAMES);
		zassert_equal(out.data.header.base_timestamp_ns, frame_ts(0, period_ns));

		for (int i = 0; i < NUM_FRAMES; i++) {
			zassert_equal(readings[i].timestamp_delta, i * period_ns);

			for (int axis = 0; axis < 3; axis++) {
				if ((axes & BIT(axis)) != 0) {
					check_value(readings[i].values[axis], out.data.shift,
						    frame_raw(i, axis));
				} else {
					zassert_equal(readings[i].values[axis], 0,
						      "axes 0x%x, frame %d, axis %d", axes, i,
						      axis);
				}
			}
		}

		for (int axis = 0; axis < 3; axis++) {
			fit = 0;

			if ((axes & BIT(axis)) == 0) {
				zassert_equal(decoder->get_frame_count(buffer, chan_axis[axis],
								       &frame_count),
					      -ENOTSUP);
				zassert_equal(
					decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_1),
					-ENOTSUP);
				continue;
			}

			zassert_ok(decoder->get_frame_count(buffer, chan_axis[axis], &frame_count));
			zassert_equal(frame_count, NUM_FRAMES);

			for (int i = 0; i < NUM_FRAMES; i++) {
				zassert_equal(
					decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_1),
					1);
				zassert_equal(out_1.header.base_timestamp_ns,
					      frame_ts(i, period_ns));
				check_value(out_1.readings[0].value, out_1.shift,
					    frame_raw(i, axis));
			}
			zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_1), 0);
		}
	}
}

ZTEST(adxl372_decoder, test_fifo_bad_frame_size)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* The header claims three axes in 4-byte frames */
	fill_fifo(ADXL372_ODR_400HZ, BIT(0) | BIT(2));
	fifo.hdr.has_y = 1;

	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

ZTEST(adxl372_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.timestamp = LAST_TS_NS;
	fifo.hdr.int_status = ADXL372_INT1_MAP_FIFO_FULL_MSK;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
		zassert_equal(frame_count, 0);
		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 0);
	}

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

ZTEST(adxl372_decoder, test_fifo_bad_odr)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL372_ODR_400HZ, BIT_MASK(3));
	fifo.hdr.accel_odr = ADXL372_ODR_6400HZ + 1;

	zassert_equal(decoder->get_frame_count(buffer, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -EINVAL);
}

#endif /* CONFIG_ADXL372_STREAM */

ZTEST_SUITE(adxl372_decoder, NULL, NULL, NULL, NULL, NULL);
