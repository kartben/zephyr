/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "mmc56x3.h"

/* Allowed error of a decoded magnetic field, in micro-Gauss */
#define TOLERANCE_UGAUSS 1
/* Allowed error of a decoded temperature, in micro-degC */
#define TOLERANCE_UCEL   10

#define MAGN_LSB_PER_GAUSS 16384
#define MAGN_MIN           (-(1 << 19))
#define MAGN_MAX           ((1 << 19) - 1)

#define TIMESTAMP_NS 123456789012ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(mmc56x3));

static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};
static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_MAGN_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_MAGN_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_MAGN_Z, 0};
static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_magn(q31_t value, int8_t shift, int32_t raw)
{
	int64_t expected = (int64_t)raw * 1000000 / MAGN_LSB_PER_GAUSS;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UGAUSS, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_temp(q31_t value, int8_t shift, uint32_t raw)
{
	int64_t expected = -75000000LL + (int64_t)raw * 800000;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UCEL, "raw %u: got %lld, expected %lld", raw,
		       actual, expected);
}

static void fill_edata(struct mmc56x3_encoded_data *edata, bool temp, bool x, bool y, bool z)
{
	memset(edata, 0, sizeof(*edata));
	edata->header.timestamp = TIMESTAMP_NS;
	edata->has_temp = temp ? 1 : 0;
	edata->has_magn_x = x ? 1 : 0;
	edata->has_magn_y = y ? 1 : 0;
	edata->has_magn_z = z ? 1 : 0;
	edata->data.temp = 125;
	edata->data.magn_x = MAGN_LSB_PER_GAUSS;
	edata->data.magn_y = MAGN_MIN;
	edata->data.magn_z = MAGN_MAX;
}

/* Decode a q31 channel, checking the header and the timestamp */
static q31_t decode_q31(const uint8_t *buffer, struct sensor_chan_spec chan, int8_t shift)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	memset(&out, 0xff, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, shift);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);

	return out.readings[0].value;
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
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENOTSUP);
}

/* A channel the buffer was not read for */
static void check_absent(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count = 1;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENODATA);
}

ZTEST(mmc56x3_decoder, test_size_info)
{
	const struct sensor_chan_spec q31_chans[] = {chan_temp, chan_x, chan_y, chan_z};
	size_t base_size;
	size_t frame_size;

	ARRAY_FOR_EACH(q31_chans, i) {
		zassert_ok(decoder->get_size_info(q31_chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
}

ZTEST(mmc56x3_decoder, test_magn_xyz)
{
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_edata(&edata, false, true, true, true);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	/* All readings in one call */
	memset(&out, 0xff, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 5);
	check_magn(out.readings[0].x, out.shift, MAGN_LSB_PER_GAUSS);
	check_magn(out.readings[0].y, out.shift, MAGN_MIN);
	check_magn(out.readings[0].z, out.shift, MAGN_MAX);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 0);

	/* One by one */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 0, &out), 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	zassert_is_null(decoder->has_trigger);

	check_absent(buffer, chan_temp);
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_X, 1});
}

ZTEST(mmc56x3_decoder, test_magn_axes)
{
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;

	fill_edata(&edata, false, true, true, true);
	edata.data.magn_x = -1;
	edata.data.magn_y = 30 * MAGN_LSB_PER_GAUSS;
	edata.data.magn_z = -30 * MAGN_LSB_PER_GAUSS;

	check_magn(decode_q31(buffer, chan_x, 5), 5, -1);
	check_magn(decode_q31(buffer, chan_y, 5), 5, 30 * MAGN_LSB_PER_GAUSS);
	check_magn(decode_q31(buffer, chan_z, 5), 5, -30 * MAGN_LSB_PER_GAUSS);
}

ZTEST(mmc56x3_decoder, test_magn_partial)
{
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;

	/* Only the X and Z axes were requested */
	fill_edata(&edata, false, true, false, true);

	check_magn(decode_q31(buffer, chan_x, 5), 5, MAGN_LSB_PER_GAUSS);
	check_magn(decode_q31(buffer, chan_z, 5), 5, MAGN_MAX);
	check_absent(buffer, chan_y);
	check_absent(buffer, chan_xyz);
	check_absent(buffer, chan_temp);
}

ZTEST(mmc56x3_decoder, test_temp)
{
	const uint32_t raw_values[] = {0, 1, 94, 125, 200, 253};
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;

	fill_edata(&edata, true, false, false, false);

	ARRAY_FOR_EACH(raw_values, i) {
		edata.data.temp = raw_values[i];
		check_temp(decode_q31(buffer, chan_temp, 7), 7, raw_values[i]);
	}

	check_absent(buffer, chan_x);
	check_absent(buffer, chan_y);
	check_absent(buffer, chan_z);
	check_absent(buffer, chan_xyz);
}

ZTEST(mmc56x3_decoder, test_temp_saturation)
{
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;

	/* 128.2 and 129 degC do not fit the output range and saturate */
	fill_edata(&edata, true, false, false, false);
	edata.data.temp = 254;
	zassert_equal(decode_q31(buffer, chan_temp, 7), INT32_MAX);
	edata.data.temp = 255;
	zassert_equal(decode_q31(buffer, chan_temp, 7), INT32_MAX);
}

ZTEST(mmc56x3_decoder, test_all)
{
	struct mmc56x3_encoded_data edata;
	const uint8_t *buffer = (const uint8_t *)&edata;

	/* SENSOR_CHAN_ALL was requested */
	fill_edata(&edata, true, true, true, true);

	check_temp(decode_q31(buffer, chan_temp, 7), 7, 125);
	check_magn(decode_q31(buffer, chan_x, 5), 5, MAGN_LSB_PER_GAUSS);
	check_magn(decode_q31(buffer, chan_y, 5), 5, MAGN_MIN);
	check_magn(decode_q31(buffer, chan_z, 5), 5, MAGN_MAX);
}

ZTEST_SUITE(mmc56x3_decoder, NULL, NULL, NULL, NULL, NULL);
