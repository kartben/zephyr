/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "paa3905.h"
#include "paa3905_decoder.h"
#include "paa3905_reg.h"

#define TIMESTAMP_NS 123456789012ULL

/* Valid bright mode frame: motion, chip OK, squal and shutter within bounds */
#define VALID_OBSERVATION ((OBSERVATION_MODE_BRIGHT << 6) | 0x3F)
#define VALID_SQUAL       0x80
#define VALID_SHUTTER     0x001000

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(paa3905));

static const struct sensor_chan_spec chan_dx = {SENSOR_CHAN_POS_DX, 0};
static const struct sensor_chan_spec chan_dy = {SENSOR_CHAN_POS_DY, 0};
static const struct sensor_chan_spec chan_dxyz = {SENSOR_CHAN_POS_DXYZ, 0};

static const uint8_t all_channels = BIT(0) | BIT(1) | BIT(2);

static struct paa3905_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static void fill(uint8_t channels, int16_t dx, int16_t dy)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.channels = channels;
	edata.buf[BURST_MOTION] = BIT(7);
	edata.buf[BURST_OBSERVATION] = VALID_OBSERVATION;
	edata.buf[BURST_SQUAL] = VALID_SQUAL;
	sys_put_be24(VALID_SHUTTER, &edata.buf[BURST_SHUTTER]);
	sys_put_le16((uint16_t)dx, &edata.buf[BURST_DELTA_X]);
	sys_put_le16((uint16_t)dy, &edata.buf[BURST_DELTA_Y]);
}

/* Convert a q31 value back to delta counts */
static void check_counts(q31_t value, int8_t shift, int16_t expected)
{
	int64_t counts = ((int64_t)value * (INT64_C(1) << shift)) >> 31;

	zassert_within(counts, expected, 0, "got %lld, expected %d", counts, expected);
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

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
}

static void check_q31(struct sensor_chan_spec chan, uint16_t max_count, int16_t expected)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	memset(&out, 0xAA, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.shift, 31);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta,
		      edata.header.timestamp);
	check_counts(out.readings[0].value, out.shift, expected);

	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 0);
}

static void check_xyz(uint16_t max_count, int16_t dx, int16_t dy)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_dxyz, &frame_count));
	zassert_equal(frame_count, 1);

	memset(&out, 0xAA, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, max_count, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.shift, 31);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta,
		      edata.header.timestamp);
	check_counts(out.readings[0].x, out.shift, dx);
	check_counts(out.readings[0].y, out.shift, dy);
	zassert_equal(out.readings[0].z, 0);

	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, max_count, &out), 0);
}

ZTEST(paa3905_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_dx, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(chan_dy, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(chan_dxyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
}

ZTEST(paa3905_decoder, test_one_shot)
{
	fill(all_channels, 1234, -32768);

	check_xyz(1, 1234, -32768);
	check_xyz(8, 1234, -32768);
	check_q31(chan_dx, 1, 1234);
	check_q31(chan_dy, 1, -32768);

	fill(all_channels, 32767, -1);

	check_xyz(1, 32767, -1);
	check_q31(chan_dx, 4, 32767);
	check_q31(chan_dy, 4, -1);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DX, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DXYZ, 1});
}

ZTEST(paa3905_decoder, test_encode)
{
	const struct sensor_chan_spec channels[] = {chan_dx, chan_dxyz};

	fill(0, -100, 200);
	zassert_ok(paa3905_encode(NULL, channels, ARRAY_SIZE(channels), (uint8_t *)&edata));
	zassert_equal(edata.header.channels, BIT(0) | BIT(2));

	check_q31(chan_dx, 1, -100);
	check_xyz(1, -100, 200);
	check_no_data(chan_dy);
}

ZTEST(paa3905_decoder, test_max_count_zero)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill(all_channels, 1, 2);

	zassert_equal(decoder->decode(buffer, chan_dxyz, &fit, 0, &out), 0);
	zassert_equal(fit, 0);
	check_xyz(1, 1, 2);
}

ZTEST(paa3905_decoder, test_channel_not_encoded)
{
	fill(BIT(0), 10, 20);

	check_q31(chan_dx, 1, 10);
	check_no_data(chan_dy);
	check_no_data(chan_dxyz);

	fill(0, 10, 20);

	check_no_data(chan_dx);
	check_no_data(chan_dy);
	check_no_data(chan_dxyz);
}

struct validity_case {
	uint8_t motion;
	uint8_t mode;
	uint8_t squal;
	uint32_t shutter;
	bool valid;
};

static const struct validity_case validity_cases[] = {
	/* No motion */
	{0x00, OBSERVATION_MODE_BRIGHT, VALID_SQUAL, VALID_SHUTTER, false},
	/* Challenging conditions */
	{BIT(7) | BIT(0), OBSERVATION_MODE_BRIGHT, VALID_SQUAL, VALID_SHUTTER, false},
	/* Invalid observation mode */
	{BIT(7), 3, VALID_SQUAL, VALID_SHUTTER, false},
	/* Squal and shutter bounds of each mode */
	{BIT(7), OBSERVATION_MODE_BRIGHT, SQUAL_MIN_BRIGHT, SHUTTER_MAX_BRIGHT - 1, true},
	{BIT(7), OBSERVATION_MODE_BRIGHT, SQUAL_MIN_BRIGHT - 1, VALID_SHUTTER, false},
	{BIT(7), OBSERVATION_MODE_BRIGHT, VALID_SQUAL, SHUTTER_MAX_BRIGHT, false},
	{BIT(7), OBSERVATION_MODE_LOW_LIGHT, SQUAL_MIN_LOW_LIGHT, SHUTTER_MAX_LOW_LIGHT - 1, true},
	{BIT(7), OBSERVATION_MODE_LOW_LIGHT, SQUAL_MIN_LOW_LIGHT - 1, VALID_SHUTTER, false},
	{BIT(7), OBSERVATION_MODE_LOW_LIGHT, VALID_SQUAL, SHUTTER_MAX_LOW_LIGHT, false},
	{BIT(7), OBSERVATION_MODE_SUPER_LOW_LIGHT, SQUAL_MIN_SUPER_LOW_LIGHT,
	 SHUTTER_MAX_SUPER_LOW_LIGHT - 1, true},
	{BIT(7), OBSERVATION_MODE_SUPER_LOW_LIGHT, SQUAL_MIN_SUPER_LOW_LIGHT - 1, VALID_SHUTTER,
	 false},
	{BIT(7), OBSERVATION_MODE_SUPER_LOW_LIGHT, 0xFF, SHUTTER_MAX_SUPER_LOW_LIGHT, false},
	/* Shutter bytes are big-endian: 0x80FF00 is above every bound */
	{BIT(7), OBSERVATION_MODE_SUPER_LOW_LIGHT, 0xFF, 0x80FF00, false},
	{BIT(7), OBSERVATION_MODE_SUPER_LOW_LIGHT, 0xFF, 0x0000FF, true},
};

ZTEST(paa3905_decoder, test_validity)
{
	for (size_t i = 0; i < ARRAY_SIZE(validity_cases); i++) {
		const struct validity_case *c = &validity_cases[i];

		TC_PRINT("case %zu\n", i);
		fill(all_channels, -5, 7);
		edata.buf[BURST_MOTION] = c->motion;
		edata.buf[BURST_OBSERVATION] = (uint8_t)((c->mode << 6) | 0x3F);
		edata.buf[BURST_SQUAL] = c->squal;
		sys_put_be24(c->shutter, &edata.buf[BURST_SHUTTER]);

		if (c->valid) {
			check_q31(chan_dx, 1, -5);
			check_q31(chan_dy, 1, 7);
			check_xyz(1, -5, 7);
		} else {
			check_no_data(chan_dx);
			check_no_data(chan_dy);
			check_no_data(chan_dxyz);
		}

		check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DZ, 0});
		check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_POS_DX, 1});
	}
}

ZTEST(paa3905_decoder, test_stream)
{
	/* Stream buffers only carry SENSOR_CHAN_POS_DXYZ */
	fill(paa3905_encode_channel(SENSOR_CHAN_POS_DXYZ), -300, 400);
	edata.header.events.drdy = true;

	check_xyz(1, -300, 400);
	check_no_data(chan_dx);
	check_no_data(chan_dy);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	edata.header.events.drdy = false;
	edata.header.events.motion = true;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	/* Trigger without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	edata.header.channels = 0;
	check_no_data(chan_dxyz);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));
}

ZTEST_SUITE(paa3905_decoder, NULL, NULL, NULL, NULL, NULL);
