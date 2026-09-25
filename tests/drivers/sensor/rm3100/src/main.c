/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "rm3100.h"
#include "rm3100_decoder.h"

/* Allowed error of a decoded value, in micro-Gauss: 1 LSB is 133 or 263 micro-Gauss */
#define TOLERANCE_UGAUSS 4

/* 75 LSB/uT at the default cycle count, 38 LSB/uT at the 600 Hz cycle count */
#define LSB_PER_GAUSS_DEFAULT  7500
#define LSB_PER_GAUSS_HIGH_ODR 3800

#define CHANNELS_XYZ (BIT(0) | BIT(1) | BIT(2))
#define TIMESTAMP_NS 123456789012ULL

#define RAW_MAX 8388607
#define RAW_MIN (-8388608)

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(rm3100));

static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_MAGN_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_MAGN_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_MAGN_Z, 0};
static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};

static const struct sensor_chan_spec unsupported[] = {
	{SENSOR_CHAN_ALL, 0},
	{SENSOR_CHAN_ACCEL_XYZ, 0},
	{SENSOR_CHAN_MAGN_XYZ, 1},
	{SENSOR_CHAN_MAGN_X, 1},
};

static struct rm3100_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static void fill(uint8_t channels, uint16_t cycle_count, int32_t x, int32_t y, int32_t z)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.channels = channels;
	edata.header.cycle_count = cycle_count;
	sys_put_be24((uint32_t)x, &edata.payload[0]);
	sys_put_be24((uint32_t)y, &edata.payload[3]);
	sys_put_be24((uint32_t)z, &edata.payload[6]);
}

static void check_value(q31_t value, int8_t shift, int32_t raw, int32_t lsb_per_gauss)
{
	int64_t expected = (int64_t)raw * 1000000 / lsb_per_gauss;
	int64_t actual = ((int64_t)value * 1000000) >> (31 - shift);

	zassert_within(actual, expected, TOLERANCE_UGAUSS, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_frame_count(struct sensor_chan_spec chan, int expected_rc)
{
	uint16_t frame_count = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), expected_rc,
		      "channel %d", chan.chan_type);
	if (expected_rc == 0) {
		zassert_equal(frame_count, 1);
	}
}

static void check_xyz(int8_t shift, const int32_t raw[3], int32_t lsb_per_gauss)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	check_frame_count(chan_xyz, 0);

	/* All readings in one call */
	memset(&out, 0xaa, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, shift);
	for (int i = 0; i < 3; i++) {
		check_value(out.readings[0].values[i], out.shift, raw[i], lsb_per_gauss);
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 0);

	/* One reading per call */
	fit = 0;
	memset(&out, 0xaa, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, TIMESTAMP_NS);
	check_value(out.readings[0].z, out.shift, raw[2], lsb_per_gauss);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
}

static void check_axes(int8_t shift, const int32_t raw[3], int32_t lsb_per_gauss)
{
	const struct sensor_chan_spec axes[] = {chan_x, chan_y, chan_z};

	for (int i = 0; i < ARRAY_SIZE(axes); i++) {
		struct sensor_q31_data out;
		uint32_t fit = 0;

		check_frame_count(axes[i], 0);

		memset(&out, 0xaa, sizeof(out));
		zassert_equal(decoder->decode(buffer, axes[i], &fit, 1, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
		zassert_equal(out.readings[0].timestamp_delta, 0);
		zassert_equal(out.shift, shift);
		check_value(out.readings[0].value, out.shift, raw[i], lsb_per_gauss);
		zassert_equal(decoder->decode(buffer, axes[i], &fit, 1, &out), 0);
	}
}

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	check_frame_count(chan, -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
}

ZTEST(rm3100_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(chan_y, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	for (int i = 0; i < ARRAY_SIZE(unsupported); i++) {
		zassert_equal(decoder->get_size_info(unsupported[i], &base_size, &frame_size),
			      -ENOTSUP);
	}
}

ZTEST(rm3100_decoder, test_default_cycle_count)
{
	const int32_t raw[3] = {LSB_PER_GAUSS_DEFAULT, RAW_MIN, RAW_MAX};

	fill(CHANNELS_XYZ, RM3100_CYCLE_COUNT_DEFAULT, raw[0], raw[1], raw[2]);

	check_xyz(11, raw, LSB_PER_GAUSS_DEFAULT);
	check_axes(11, raw, LSB_PER_GAUSS_DEFAULT);
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

ZTEST(rm3100_decoder, test_high_odr_cycle_count)
{
	const int32_t raw[3] = {-12345, RAW_MAX, -LSB_PER_GAUSS_HIGH_ODR};

	fill(CHANNELS_XYZ, RM3100_CYCLE_COUNT_HIGH_ODR, raw[0], raw[1], raw[2]);

	check_xyz(12, raw, LSB_PER_GAUSS_HIGH_ODR);
	check_axes(12, raw, LSB_PER_GAUSS_HIGH_ODR);
}

ZTEST(rm3100_decoder, test_channel_subset)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	/* One-shot read of SENSOR_CHAN_MAGN_X only */
	fill(BIT(0), RM3100_CYCLE_COUNT_DEFAULT, 75, 0, 0);

	check_frame_count(chan_x, 0);
	zassert_equal(decoder->decode(buffer, chan_x, &fit, 1, &out), 1);
	check_value(out.readings[0].value, out.shift, 75, LSB_PER_GAUSS_DEFAULT);

	check_no_data(chan_y);
	check_no_data(chan_z);
	check_no_data(chan_xyz);

	fill(BIT(1) | BIT(2), RM3100_CYCLE_COUNT_DEFAULT, 0, 0, 0);
	check_frame_count(chan_z, 0);
	check_no_data(chan_x);
	check_no_data(chan_xyz);
}

ZTEST(rm3100_decoder, test_stream)
{
	const int32_t raw[3] = {1, -1, 0};

	fill(CHANNELS_XYZ, RM3100_CYCLE_COUNT_DEFAULT, raw[0], raw[1], raw[2]);
	edata.header.status = BIT(7);
	edata.header.events.drdy = true;

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	check_xyz(11, raw, LSB_PER_GAUSS_DEFAULT);

	/* SENSOR_STREAM_DATA_DROP or _NOP: the data is not reported */
	edata.header.channels = 0;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	check_no_data(chan_x);
	check_no_data(chan_y);
	check_no_data(chan_z);
	check_no_data(chan_xyz);
}

ZTEST(rm3100_decoder, test_unsupported)
{
	struct sensor_three_axis_data out;

	fill(CHANNELS_XYZ, RM3100_CYCLE_COUNT_DEFAULT, 0, 0, 0);

	for (int i = 0; i < ARRAY_SIZE(unsupported); i++) {
		uint16_t frame_count;
		uint32_t fit = 0;

		zassert_equal(decoder->get_frame_count(buffer, unsupported[i], &frame_count),
			      -ENOTSUP);
		zassert_equal(decoder->decode(buffer, unsupported[i], &fit, 1, &out), -ENOTSUP);
	}
}

static uint64_t clock_now_ns(void)
{
	uint64_t cycles;

	zassert_ok(sensor_clock_get_cycles(&cycles));

	return sensor_clock_cycles_to_ns(cycles);
}

static void check_encode(const struct device *dev, const struct sensor_chan_spec *chans,
			 size_t num_chans, uint8_t channels, uint16_t cycle_count)
{
	uint64_t before = clock_now_ns();

	/* The encoding does not rely on a zeroed buffer */
	memset(&edata, 0xff, sizeof(edata));

	zassert_ok(rm3100_encode(dev, chans, num_chans, (uint8_t *)&edata));
	zassert_equal(edata.header.channels, channels);
	zassert_equal(edata.header.cycle_count, cycle_count);
	zassert_equal(edata.header.status, 0);
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(edata.header.timestamp >= before);
	zassert_true(edata.header.timestamp <= clock_now_ns());
}

ZTEST(rm3100_decoder, test_encode)
{
	const struct sensor_chan_spec xy[] = {chan_x, chan_y};
	const struct sensor_chan_spec all = {SENSOR_CHAN_ALL, 0};

	check_encode(DEVICE_DT_GET(DT_NODELABEL(rm3100)), &chan_xyz, 1, CHANNELS_XYZ,
		     RM3100_CYCLE_COUNT_DEFAULT);
	check_encode(DEVICE_DT_GET(DT_NODELABEL(rm3100)), xy, ARRAY_SIZE(xy), BIT(0) | BIT(1),
		     RM3100_CYCLE_COUNT_DEFAULT);
	check_encode(DEVICE_DT_GET(DT_NODELABEL(rm3100_600hz)), &all, 1, CHANNELS_XYZ,
		     RM3100_CYCLE_COUNT_HIGH_ODR);
	check_encode(DEVICE_DT_GET(DT_NODELABEL(rm3100_600hz)), &chan_z, 1, BIT(2),
		     RM3100_CYCLE_COUNT_HIGH_ODR);
}

ZTEST_SUITE(rm3100_decoder, NULL, NULL, NULL, NULL, NULL);
