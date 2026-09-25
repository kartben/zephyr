/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adxl345.h"

/* Allowed error of a decoded value, in micro-m/s^2 */
#define TOLERANCE_UMS2 1000

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl345));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static int64_t raw_to_micro(int16_t raw, int32_t lsb_per_g)
{
	return (int64_t)raw * SENSOR_G / lsb_per_g;
}

static void check_value(q31_t value, int8_t shift, int16_t raw, int32_t lsb_per_g)
{
	int64_t expected = raw_to_micro(raw, lsb_per_g);
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void put_frame(uint8_t *frame, int16_t x, int16_t y, int16_t z)
{
	sys_put_le16((uint16_t)x, &frame[0]);
	sys_put_le16((uint16_t)y, &frame[2]);
	sys_put_le16((uint16_t)z, &frame[4]);
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

ZTEST(adxl345_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0},
					  &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

ZTEST(adxl345_decoder, test_single_full_res)
{
	struct adxl345_sample sample = {
		.selected_range = ADXL345_RANGE_8G,
		.is_full_res = true,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Full resolution: 256 LSB/g, 12 bits at +/-8 g */
	put_frame(sample.axis_data, 256, -512, -2048);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.shift, 7);
	check_value(out.readings[0].x, out.shift, 256, 256);
	check_value(out.readings[0].y, out.shift, -512, 256);
	check_value(out.readings[0].z, out.shift, -2048, 256);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0},
				      &fit, 1, &out_1),
		      1);
	check_value(out_1.readings[0].value, out_1.shift, -512, 256);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(adxl345_decoder, test_single_10bit)
{
	struct adxl345_sample sample = {
		.selected_range = ADXL345_RANGE_16G,
		.is_full_res = false,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* 10-bit mode at +/-16 g: 32 LSB/g */
	put_frame(sample.axis_data, 32, -512, 511);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.shift, 8);
	check_value(out.readings[0].x, out.shift, 32, 32);
	check_value(out.readings[0].y, out.shift, -512, 32);
	check_value(out.readings[0].z, out.shift, 511, 32);
}

#ifdef CONFIG_ADXL345_STREAM

#define NUM_FRAMES 3
#define LAST_TS_NS 1000000000ULL

static struct {
	struct adxl345_fifo_data hdr;
	uint8_t frames[NUM_FRAMES * SAMPLE_SIZE];
} __packed fifo;

static void fill_fifo(enum adxl345_odr odr)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.is_full_res = 1;
	fifo.hdr.selected_range = ADXL345_RANGE_2G;
	fifo.hdr.sample_set_size = SAMPLE_SIZE;
	fifo.hdr.int_status = ADXL345_INT_MAP_WATERMARK_MSK;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.fifo_byte_count = sizeof(fifo.frames);
	fifo.hdr.timestamp = LAST_TS_NS;

	for (int i = 0; i < NUM_FRAMES; i++) {
		put_frame(&fifo.frames[i * SAMPLE_SIZE], i * 10, -i * 10, 256);
	}
}

ZTEST(adxl345_decoder, test_fifo)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL345_ODR_100HZ);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES);
	zassert_equal(out.data.header.base_timestamp_ns, LAST_TS_NS - 2 * 10000000ULL);
	zassert_equal(out.data.shift, 5);

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(readings[i].timestamp_delta, i * 10000000U);
		check_value(readings[i].x, out.data.shift, i * 10, 256);
		check_value(readings[i].y, out.data.shift, -i * 10, 256);
		check_value(readings[i].z, out.data.shift, 256, 256);
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_FRAMES, &out), 0);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(adxl345_decoder, test_fifo_12_5hz)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill_fifo(ADXL345_ODR_12_5HZ);

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
		zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta,
			      LAST_TS_NS - (NUM_FRAMES - 1 - i) * 80000000ULL);
	}
}

ZTEST(adxl345_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.int_status = ADXL345_INT_MAP_WATERMARK_MSK;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

#endif /* CONFIG_ADXL345_STREAM */

ZTEST_SUITE(adxl345_decoder, NULL, NULL, NULL, NULL, NULL);
