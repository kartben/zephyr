/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "als31300.h"

/* Allowed error of a decoded value, in micro-units (microgauss or micro-degrees Celsius) */
#define TOLERANCE_MICRO 100

#define TIMESTAMP_NS 1234567890123ULL

#define CHAN_BIT_X    BIT(0)
#define CHAN_BIT_Y    BIT(1)
#define CHAN_BIT_Z    BIT(2)
#define CHAN_BIT_TEMP BIT(3)
#define CHAN_BIT_ALL  (CHAN_BIT_X | CHAN_BIT_Y | CHAN_BIT_Z | CHAN_BIT_TEMP)

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(als31300));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};
static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_MAGN_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_MAGN_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_MAGN_Z, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};

/* Encoded data at an odd address, to check that the decoder handles unaligned buffers */
static struct {
	uint8_t pad;
	struct als31300_encoded_data edata;
} __packed enc;

static const uint8_t *const buffer = (const uint8_t *)&enc.edata;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_magn(q31_t value, int8_t shift, int16_t raw)
{
	/* ALS31300-500: 4 LSB/G */
	int64_t expected = (int64_t)raw * 1000000 / 4;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_temp(q31_t value, int8_t shift, uint16_t raw)
{
	/* T = 302 * (raw - 1708) / 4096 degrees Celsius */
	int64_t expected = (int64_t)302 * ((int32_t)raw - 1708) * 1000000 / 4096;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "raw %u: got %lld, expected %lld", raw,
		       actual, expected);
}

/* Fill the payload with registers 0x28 and 0x29 as read from the bus, MSB first */
static void fill(uint8_t channels, int16_t x, int16_t y, int16_t z, uint16_t temp)
{
	uint16_t ux = (uint16_t)x & 0xfffU;
	uint16_t uy = (uint16_t)y & 0xfffU;
	uint16_t uz = (uint16_t)z & 0xfffU;
	uint32_t reg28;
	uint32_t reg29;

	reg28 = FIELD_PREP(ALS31300_REG28_X_AXIS_MSB_MASK, ux >> 4) |
		FIELD_PREP(ALS31300_REG28_Y_AXIS_MSB_MASK, uy >> 4) |
		FIELD_PREP(ALS31300_REG28_Z_AXIS_MSB_MASK, uz >> 4) |
		FIELD_PREP(ALS31300_REG28_NEW_DATA_MASK, 1U) |
		FIELD_PREP(ALS31300_REG28_TEMP_MSB_MASK, temp >> 6);
	reg29 = FIELD_PREP(ALS31300_REG29_X_AXIS_LSB_MASK, ux & 0xfU) |
		FIELD_PREP(ALS31300_REG29_Y_AXIS_LSB_MASK, uy & 0xfU) |
		FIELD_PREP(ALS31300_REG29_Z_AXIS_LSB_MASK, uz & 0xfU) |
		FIELD_PREP(ALS31300_REG29_TEMP_LSB_MASK, temp & 0x3fU);

	memset(&enc, 0xa5, sizeof(enc));
	enc.edata.header.channels = channels;
	enc.edata.header.timestamp = TIMESTAMP_NS;
	sys_put_be32(reg28, &enc.edata.payload[0]);
	sys_put_be32(reg29, &enc.edata.payload[4]);
}

static void check_frame_count(struct sensor_chan_spec chan, int expected_rc)
{
	uint16_t frame_count = 0xffff;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), expected_rc,
		      "channel %d", chan.chan_type);
	if (expected_rc == 0) {
		zassert_equal(frame_count, 1);
	}
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP,
		      "channel %d idx %d", chan.chan_type, chan.chan_idx);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP,
		      "channel %d idx %d", chan.chan_type, chan.chan_idx);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP, "channel %d idx %d",
		      chan.chan_type, chan.chan_idx);
}

/* Decode a single axis or the temperature, as a sensor_q31_data */
static q31_t decode_q31(struct sensor_chan_spec chan, int8_t *shift)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1, "channel %d",
		      chan.chan_type);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, TIMESTAMP_NS);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
	*shift = out.shift;

	return out.readings[0].value;
}

static void check_sample(int16_t x, int16_t y, int16_t z, uint16_t temp)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;
	int8_t shift;
	q31_t value;

	fill(CHAN_BIT_ALL, x, y, z, temp);

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 16);
	check_magn(out.readings[0].x, out.shift, x);
	check_magn(out.readings[0].y, out.shift, y);
	check_magn(out.readings[0].z, out.shift, z);

	value = decode_q31(chan_x, &shift);
	zassert_equal(shift, 16);
	check_magn(value, shift, x);

	value = decode_q31(chan_y, &shift);
	zassert_equal(shift, 16);
	check_magn(value, shift, y);

	value = decode_q31(chan_z, &shift);
	zassert_equal(shift, 16);
	check_magn(value, shift, z);

	value = decode_q31(chan_temp, &shift);
	zassert_equal(shift, 16);
	check_temp(value, shift, temp);
}

ZTEST(als31300_decoder, test_size_info)
{
	const struct sensor_chan_spec q31_chans[] = {chan_x, chan_y, chan_z, chan_temp};
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	ARRAY_FOR_EACH(q31_chans, i) {
		zassert_ok(decoder->get_size_info(q31_chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(als31300_decoder, test_values)
{
	check_sample(100, -200, 300, 1708);
	check_sample(-2048, 2047, -1, 0);
	check_sample(2047, -2048, 1, 4095);
	check_sample(0, 0, 0, 2048);
}

ZTEST(als31300_decoder, test_frame_count)
{
	fill(CHAN_BIT_ALL, 1, 2, 3, 1708);

	check_frame_count(chan_xyz, 0);
	check_frame_count(chan_x, 0);
	check_frame_count(chan_y, 0);
	check_frame_count(chan_z, 0);
	check_frame_count(chan_temp, 0);
}

ZTEST(als31300_decoder, test_channel_not_read)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;
	int8_t shift;
	q31_t value;

	/* Only SENSOR_CHAN_MAGN_Y was requested in the read */
	fill(CHAN_BIT_Y, 10, -20, 30, 1708);

	check_frame_count(chan_y, 0);
	value = decode_q31(chan_y, &shift);
	check_magn(value, shift, -20);

	check_frame_count(chan_xyz, -ENODATA);
	check_frame_count(chan_x, -ENODATA);
	check_frame_count(chan_z, -ENODATA);
	check_frame_count(chan_temp, -ENODATA);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), -ENODATA);

	/* Only the temperature was requested */
	fill(CHAN_BIT_TEMP, 10, -20, 30, 1000);

	check_frame_count(chan_temp, 0);
	check_frame_count(chan_xyz, -ENODATA);
	value = decode_q31(chan_temp, &shift);
	check_temp(value, shift, 1000);
}

ZTEST(als31300_decoder, test_max_count)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[3];
	} out;
	uint32_t fit = 0;

	fill(CHAN_BIT_ALL, -5, 6, -7, 1708);

	/* No reading fits */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 0, &out), 0);
	zassert_equal(fit, 0);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.data.readings[0].timestamp_delta, 0);
	check_magn(out.data.readings[0].x, out.data.shift, -5);
	check_magn(out.data.readings[0].y, out.data.shift, 6);
	check_magn(out.data.readings[0].z, out.data.shift, -7);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 4, &out), 0);
}

ZTEST(als31300_decoder, test_unsupported)
{
	fill(CHAN_BIT_ALL, 1, 2, 3, 1708);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
}

ZTEST(als31300_decoder, test_has_trigger)
{
	/* The driver has no trigger or stream support */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(als31300_decoder, NULL, NULL, NULL, NULL, NULL);
