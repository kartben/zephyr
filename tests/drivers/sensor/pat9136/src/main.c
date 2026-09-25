/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pat9136.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "pat9136.h"
#include "pat9136_decoder.h"

/* Allowed error of a decoded distance, in nm */
#define TOLERANCE_NM 20

#define TIMESTAMP_NS 123456789012ULL

#define MOTION_DETECTED   0x80U
#define OBSERVATION_VALID 0xB7U

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(pat9136));

static const struct sensor_chan_spec chan_dx = {SENSOR_CHAN_POS_DX, 0};
static const struct sensor_chan_spec chan_dy = {SENSOR_CHAN_POS_DY, 0};
static const struct sensor_chan_spec chan_dxyz = {SENSOR_CHAN_POS_DXYZ, 0};
static const struct sensor_chan_spec chan_dx_mm = {SENSOR_CHAN_POS_DX_MM, 0};
static const struct sensor_chan_spec chan_dy_mm = {SENSOR_CHAN_POS_DY_MM, 0};
static const struct sensor_chan_spec chan_dxyz_mm = {SENSOR_CHAN_POS_DXYZ_MM, 0};

static struct pat9136_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static void fill(uint8_t channels, uint16_t res_x, uint16_t res_y, int16_t dx, int16_t dy)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.channels = channels;
	sys_put_le16(res_x, &edata.header.resolution.buf[0]);
	sys_put_le16(res_y, &edata.header.resolution.buf[2]);

	/* Burst read: motion, observation, delta X, delta Y, ... */
	edata.buf[0] = MOTION_DETECTED;
	edata.buf[1] = OBSERVATION_VALID;
	sys_put_le16((uint16_t)dx, &edata.buf[2]);
	sys_put_le16((uint16_t)dy, &edata.buf[4]);
}

/* Convert a q31 value in mm to nm */
static int64_t q31_mm_to_nm(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000LL) >> (31 - shift);
}

/* One count is 25.4 mm / CPI, with CPI = (resolution + 1) * 100 */
static void check_mm(q31_t value, int8_t shift, int16_t raw, uint16_t res)
{
	int64_t expected = (int64_t)raw * 25400000LL / ((res + 1) * 100);
	int64_t actual = q31_mm_to_nm(value, shift);

	zassert_within(actual, expected, TOLERANCE_NM, "raw %d res %u: got %lld nm, expected %lld",
		       raw, res, actual, expected);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENOTSUP);
}

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENODATA);
}

static void check_size(struct sensor_chan_spec chan, size_t base, size_t frame)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan, &base_size, &frame_size));
	zassert_equal(base_size, base);
	zassert_equal(frame_size, frame);
}

ZTEST(pat9136_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	check_size(chan_dx, sizeof(struct sensor_q31_data), sizeof(struct sensor_q31_sample_data));
	check_size(chan_dy, sizeof(struct sensor_q31_data), sizeof(struct sensor_q31_sample_data));
	check_size(chan_dx_mm, sizeof(struct sensor_q31_data),
		   sizeof(struct sensor_q31_sample_data));
	check_size(chan_dy_mm, sizeof(struct sensor_q31_data),
		   sizeof(struct sensor_q31_sample_data));
	check_size(chan_dxyz, sizeof(struct sensor_three_axis_data),
		   sizeof(struct sensor_three_axis_sample_data));
	check_size(chan_dxyz_mm, sizeof(struct sensor_three_axis_data),
		   sizeof(struct sensor_three_axis_sample_data));

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

ZTEST(pat9136_decoder, test_counts)
{
	struct sensor_q31_data out;
	struct sensor_three_axis_data out3;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 19, 19, -1234, 32767);

	zassert_ok(decoder->get_frame_count(buffer, chan_dx, &frame_count));
	zassert_equal(frame_count, 1);

	/* Unwritten fields must be set by the decoder */
	memset(&out, 0xff, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_dx, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 31);
	zassert_equal(out.readings[0].value, -1234);
	zassert_equal(decoder->decode(buffer, chan_dx, &fit, 1, &out), 0);

	fit = 0;
	zassert_ok(decoder->get_frame_count(buffer, chan_dy, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(buffer, chan_dy, &fit, 8, &out), 1);
	zassert_equal(out.shift, 31);
	zassert_equal(out.readings[0].value, 32767);
	zassert_equal(decoder->decode(buffer, chan_dy, &fit, 8, &out), 0);

	fit = 0;
	memset(&out3, 0xff, sizeof(out3));
	zassert_ok(decoder->get_frame_count(buffer, chan_dxyz, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), 1);
	zassert_equal(out3.header.reading_count, 1);
	zassert_equal(out3.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out3.readings[0].timestamp_delta, 0);
	zassert_equal(out3.shift, 31);
	zassert_equal(out3.readings[0].x, -1234);
	zassert_equal(out3.readings[0].y, 32767);
	zassert_equal(out3.readings[0].z, 0);
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), 0);

	/* No readings requested */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_dx, &fit, 0, &out), 0);
}

static const struct {
	uint16_t res;
	int8_t shift;
} mm_shifts[] = {
	{0, 14}, {1, 13},  {3, 12},  {4, 11},  {19, 9},  {63, 8},
	{64, 8}, {127, 7}, {128, 7}, {129, 7}, {130, 6}, {199, 6},
};

static const int16_t mm_raw[] = {1, -1, 1000, -1234, 32767, -32768};

ZTEST(pat9136_decoder, test_mm)
{
	struct sensor_q31_data out;
	struct sensor_three_axis_data out3;
	uint16_t frame_count;
	uint32_t fit;

	for (size_t i = 0; i < ARRAY_SIZE(mm_shifts); i++) {
		uint16_t res = mm_shifts[i].res;

		for (size_t j = 0; j < ARRAY_SIZE(mm_raw); j++) {
			int16_t raw = mm_raw[j];

			fill(pat9136_encode_channel(SENSOR_CHAN_ALL), res, res, raw, -raw / 2);

			fit = 0;
			zassert_ok(decoder->get_frame_count(buffer, chan_dx_mm, &frame_count));
			zassert_equal(frame_count, 1);
			zassert_equal(decoder->decode(buffer, chan_dx_mm, &fit, 1, &out), 1);
			zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
			zassert_equal(out.readings[0].timestamp_delta, 0);
			zassert_equal(out.shift, mm_shifts[i].shift, "res %u: shift %d", res,
				      out.shift);
			check_mm(out.readings[0].value, out.shift, raw, res);

			fit = 0;
			zassert_equal(decoder->decode(buffer, chan_dy_mm, &fit, 1, &out), 1);
			zassert_equal(out.shift, mm_shifts[i].shift);
			check_mm(out.readings[0].value, out.shift, -raw / 2, res);

			fit = 0;
			zassert_equal(decoder->decode(buffer, chan_dxyz_mm, &fit, 1, &out3), 1);
			zassert_equal(out3.header.base_timestamp_ns, TIMESTAMP_NS);
			zassert_equal(out3.readings[0].timestamp_delta, 0);
			zassert_equal(out3.shift, mm_shifts[i].shift);
			check_mm(out3.readings[0].x, out3.shift, raw, res);
			check_mm(out3.readings[0].y, out3.shift, -raw / 2, res);
			zassert_equal(out3.readings[0].z, 0);
			zassert_equal(decoder->decode(buffer, chan_dxyz_mm, &fit, 1, &out3), 0);
		}
	}
}

/* Resolutions the device is not configured with, such as garbage upper bytes */
static const struct {
	uint16_t res;
	int8_t shift;
} mm_high_shifts[] = {
	{200, 6}, {255, 6}, {256, 6}, {327, 5}, {4660, 1}, {65535, 0},
};

ZTEST(pat9136_decoder, test_mm_high_resolution)
{
	struct sensor_three_axis_data out3;
	uint32_t fit;

	for (size_t i = 0; i < ARRAY_SIZE(mm_high_shifts); i++) {
		uint16_t res = mm_high_shifts[i].res;

		for (size_t j = 0; j < ARRAY_SIZE(mm_raw); j++) {
			int16_t raw = mm_raw[j];

			fill(pat9136_encode_channel(SENSOR_CHAN_ALL), res, res, raw, -raw / 2);

			fit = 0;
			zassert_equal(decoder->decode(buffer, chan_dxyz_mm, &fit, 1, &out3), 1);
			zassert_equal(out3.shift, mm_high_shifts[i].shift, "res %u: shift %d", res,
				      out3.shift);
			check_mm(out3.readings[0].x, out3.shift, raw, res);
			check_mm(out3.readings[0].y, out3.shift, -raw / 2, res);
		}
	}
}

ZTEST(pat9136_decoder, test_mm_per_axis_resolution)
{
	struct sensor_q31_data out;
	struct sensor_three_axis_data out3;
	uint32_t fit = 0;

	/* The shift covers the lowest resolution, each axis is scaled with its own */
	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 64, 19, 32767, -20000);

	zassert_equal(decoder->decode(buffer, chan_dxyz_mm, &fit, 4, &out3), 1);
	zassert_equal(out3.header.reading_count, 1);
	zassert_equal(out3.shift, 9);
	check_mm(out3.readings[0].x, out3.shift, 32767, 64);
	check_mm(out3.readings[0].y, out3.shift, -20000, 19);
	zassert_equal(out3.readings[0].z, 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_dx_mm, &fit, 1, &out), 1);
	zassert_equal(out.shift, 9);
	check_mm(out.readings[0].value, out.shift, 32767, 64);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_dy_mm, &fit, 1, &out), 1);
	zassert_equal(out.shift, 9);
	check_mm(out.readings[0].value, out.shift, -20000, 19);
}

ZTEST(pat9136_decoder, test_no_data)
{
	struct sensor_three_axis_data out3;
	uint32_t fit = 0;

	/* Stream buffers only carry the 3-axis channels */
	fill(pat9136_encode_channel(SENSOR_CHAN_POS_DXYZ) |
		     pat9136_encode_channel(SENSOR_CHAN_POS_DXYZ_MM),
	     19, 19, 10, 20);

	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), 1);
	zassert_equal(out3.readings[0].x, 10);
	zassert_equal(out3.readings[0].y, 20);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_dxyz_mm, &fit, 1, &out3), 1);
	check_mm(out3.readings[0].x, out3.shift, 10, 19);
	check_mm(out3.readings[0].y, out3.shift, 20, 19);

	check_no_data(chan_dx);
	check_no_data(chan_dy);
	check_no_data(chan_dx_mm);
	check_no_data(chan_dy_mm);

	/* Buffer without channels */
	fill(0, 19, 19, 10, 20);
	check_no_data(chan_dxyz);
	check_no_data(chan_dxyz_mm);

	/* No motion */
	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 19, 19, 0, 0);
	edata.buf[0] = 0;
	check_no_data(chan_dx);
	check_no_data(chan_dxyz);
	check_no_data(chan_dxyz_mm);

	/* Invalid observation register */
	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 19, 19, 10, 20);
	edata.buf[1] = 0xA5;
	check_no_data(chan_dy);
	check_no_data(chan_dxyz);

	/* The other valid observation value */
	edata.buf[1] = 0xBF;
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), 1);
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), 0);

	/* Invalid data is reported also once the reading has been decoded */
	edata.buf[0] = 0;
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 1, &out3), -ENODATA);
}

ZTEST(pat9136_decoder, test_unsupported)
{
	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 19, 19, 10, 20);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DX, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DXYZ_MM, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});

	/* The channel is checked before the data */
	edata.buf[0] = 0;
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DX, 1});
}

ZTEST(pat9136_decoder, test_has_trigger)
{
	fill(pat9136_encode_channel(SENSOR_CHAN_ALL), 19, 19, 10, 20);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	edata.header.events.drdy = true;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	edata.header.events.drdy = false;
	edata.header.events.motion = true;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST_SUITE(pat9136_decoder, NULL, NULL, NULL, NULL, NULL);
