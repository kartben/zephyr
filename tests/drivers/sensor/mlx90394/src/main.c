/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "mlx90394.h"

/* Allowed error of a decoded value, in micro-gauss or micro-degrees Celsius */
#define TOLERANCE_MICRO 2

#define TIMESTAMP_NS 123456789012ULL

/* Micro-units per LSB */
#define HIGH_RANGE_UG       15000
#define HIGH_SENSITIVITY_UG 1500
#define TEMP_UC             20000

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(mlx90394));

static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_MAGN_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_MAGN_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_MAGN_Z, 0};
static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};

static struct mlx90394_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) / (INT64_C(1) << (31 - shift));
}

static void check_value(q31_t value, int8_t shift, int16_t raw, int32_t micro_per_lsb)
{
	int64_t expected = (int64_t)raw * micro_per_lsb;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void fill(enum mlx90394_reg_config_val config_val, enum sensor_channel measured, int16_t x,
		 int16_t y, int16_t z, int16_t temp)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.config_val = config_val;
	edata.header.channel = measured;
	sys_put_le16((uint16_t)x, &edata.readings[0]);
	sys_put_le16((uint16_t)y, &edata.readings[2]);
	sys_put_le16((uint16_t)z, &edata.readings[4]);
	sys_put_le16((uint16_t)temp, &edata.readings[6]);
}

static void check_frame_count(struct sensor_chan_spec chan, uint16_t expected)
{
	uint16_t frame_count = UINT16_MAX;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, expected, "channel %d: %u frames", chan.chan_type, frame_count);
}

static void check_xyz(int8_t shift, int32_t micro_per_lsb, int16_t x, int16_t y, int16_t z)
{
	/* All readings in one call, then one by one */
	const uint16_t max_counts[] = {4, 1};
	struct sensor_three_axis_data out;
	uint32_t fit;

	check_frame_count(chan_xyz, 1);

	ARRAY_FOR_EACH(max_counts, i) {
		uint16_t max_count = max_counts[i];

		memset(&out, 0xa5, sizeof(out));
		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, max_count, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta,
			      TIMESTAMP_NS);
		zassert_equal(out.shift, shift);
		check_value(out.readings[0].x, out.shift, x, micro_per_lsb);
		check_value(out.readings[0].y, out.shift, y, micro_per_lsb);
		check_value(out.readings[0].z, out.shift, z, micro_per_lsb);
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, max_count, &out), 0);
	}
}

static void check_q31(struct sensor_chan_spec chan, int8_t shift, int32_t micro_per_lsb,
		      int16_t raw)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	check_frame_count(chan, 1);

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, TIMESTAMP_NS);
	zassert_equal(out.shift, shift);
	check_value(out.readings[0].value, out.shift, raw, micro_per_lsb);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	check_frame_count(chan, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
	zassert_equal(fit, 0);
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

static void check_size_info(struct sensor_chan_spec chan, size_t base, size_t frame)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan, &base_size, &frame_size));
	zassert_equal(base_size, base);
	zassert_equal(frame_size, frame);
}

ZTEST(mlx90394_decoder, test_size_info)
{
	check_size_info(chan_xyz, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));
	check_size_info(chan_x, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(chan_y, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(chan_z, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(chan_temp, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
}

ZTEST(mlx90394_decoder, test_high_range)
{
	const enum mlx90394_reg_config_val configs[] = {
		MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_CURRENT,
		MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE,
	};

	ARRAY_FOR_EACH(configs, i) {
		/* 15 mG/LSB, up to +/-491.52 G */
		fill(configs[i], SENSOR_CHAN_ALL, 1000, -32768, 32767, 1250);

		check_xyz(MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 1000, -32768, 32767);
		check_q31(chan_x, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 1000);
		check_q31(chan_y, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, -32768);
		check_q31(chan_z, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 32767);
		check_q31(chan_temp, MLX90394_SHIFT_TEMP, TEMP_UC, 1250);
	}
}

ZTEST(mlx90394_decoder, test_high_sensitivity)
{
	/* 1.5 mG/LSB, up to +/-49.152 G */
	fill(MLX90394_CTRL2_CONFIG_HIGH_SENSITIVITY_LOW_NOISE, SENSOR_CHAN_ALL, -1, 32767, -32768,
	     -2000);

	check_xyz(MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY, HIGH_SENSITIVITY_UG, -1, 32767, -32768);
	check_q31(chan_x, MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY, HIGH_SENSITIVITY_UG, -1);
	check_q31(chan_y, MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY, HIGH_SENSITIVITY_UG, 32767);
	check_q31(chan_z, MLX90394_SHIFT_MAGN_HIGH_SENSITIVITY, HIGH_SENSITIVITY_UG, -32768);

	/* The temperature scale does not depend on the magnetometer configuration */
	check_q31(chan_temp, MLX90394_SHIFT_TEMP, TEMP_UC, -2000);
}

ZTEST(mlx90394_decoder, test_temp_limits)
{
	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_AMBIENT_TEMP, 0, 0, 0, 32767);
	check_q31(chan_temp, MLX90394_SHIFT_TEMP, TEMP_UC, 32767);

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_AMBIENT_TEMP, 0, 0, 0, -32768);
	check_q31(chan_temp, MLX90394_SHIFT_TEMP, TEMP_UC, -32768);
}

ZTEST(mlx90394_decoder, test_measured_channel)
{
	/* Only the values of the measured channel are decoded */
	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_MAGN_X, 100, 200, 300, 400);
	check_q31(chan_x, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 100);
	check_no_data(chan_y);
	check_no_data(chan_z);
	check_no_data(chan_xyz);
	check_no_data(chan_temp);

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_MAGN_Y, 100, 200, 300, 400);
	check_q31(chan_y, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 200);
	check_no_data(chan_x);
	check_no_data(chan_z);
	check_no_data(chan_xyz);
	check_no_data(chan_temp);

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_MAGN_Z, 100, 200, 300, 400);
	check_q31(chan_z, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 300);
	check_no_data(chan_x);
	check_no_data(chan_y);
	check_no_data(chan_xyz);
	check_no_data(chan_temp);

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_MAGN_XYZ, 100, 200, 300, 400);
	check_xyz(MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 100, 200, 300);
	check_q31(chan_x, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 100);
	check_q31(chan_y, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 200);
	check_q31(chan_z, MLX90394_SHIFT_MAGN_HIGH_RANGE, HIGH_RANGE_UG, 300);
	check_no_data(chan_temp);

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_AMBIENT_TEMP, 100, 200, 300,
	     400);
	check_q31(chan_temp, MLX90394_SHIFT_TEMP, TEMP_UC, 400);
	check_no_data(chan_x);
	check_no_data(chan_y);
	check_no_data(chan_z);
	check_no_data(chan_xyz);
}

ZTEST(mlx90394_decoder, test_max_count_zero)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_ALL, 1, 2, 3, 4);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 0, &out), 0);
	zassert_equal(fit, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
}

ZTEST(mlx90394_decoder, test_unsupported)
{
	fill(MLX90394_CTRL2_CONFIG_HIGH_RANGE_LOW_NOISE, SENSOR_CHAN_ALL, 1, 2, 3, 4);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_X, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});

	/* The sensor has no FIFO and no trigger */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(mlx90394_decoder, NULL, NULL, NULL, NULL, NULL);
