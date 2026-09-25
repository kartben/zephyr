/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/pcnt_esp32.h>
#include <zephyr/ztest.h>

#include "pcnt_esp32_decoder.h"

#define TIMESTAMP_NS 123456789000ULL

/* Allowed error of a decoded angle, in micro-degrees */
#define TOLERANCE_UDEG 1

/* Buffer entries: the position in the arrays differs from the unit index */
#define POS_UNIT2 0
#define POS_UNIT0 1
#define POS_UNIT3 2

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(pcnt));

static struct pcnt_esp32_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static struct sensor_chan_spec rotation(uint16_t unit)
{
	return (struct sensor_chan_spec){SENSOR_CHAN_ROTATION, unit};
}

static struct sensor_chan_spec count(uint16_t unit)
{
	return (struct sensor_chan_spec){SENSOR_CHAN_ENCODER_COUNT, unit};
}

static void set_unit(int pos, uint8_t unit, int32_t value, uint32_t counts_per_rev)
{
	edata.unit_idx[pos] = unit;
	edata.counts[pos] = value;
	edata.counts_per_rev[pos] = counts_per_rev;
}

static void *setup(void)
{
	zassert_equal(PCNT_ESP32_MAX_UNITS, 4);

	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(&edata, 0, sizeof(edata));
	edata.timestamp_ns = TIMESTAMP_NS;
	edata.num_units = 3;
	set_unit(POS_UNIT2, 2, -4567, 0);
	set_unit(POS_UNIT0, 0, 100, 400);
	set_unit(POS_UNIT3, 3, 0, 0);
}

/* Decode the single reading of a channel, checking the header and the timestamp */
static void decode_one(struct sensor_chan_spec chan, uint16_t max_count, q31_t *value,
		       int8_t *shift)
{
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[3];
	} out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Catch fields left unwritten */
	memset(&out, 0xa5, sizeof(out));

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns + out.data.readings[0].timestamp_delta,
		      TIMESTAMP_NS);
	*value = out.data.readings[0].value;
	*shift = out.data.shift;

	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out), 0);
}

static void check_count(struct sensor_chan_spec chan, int32_t expected)
{
	q31_t value;
	int8_t shift;

	decode_one(chan, 1, &value, &shift);
	zassert_equal(shift, 0);
	zassert_equal(value, expected, "got %d, expected %d", value, expected);

	decode_one(chan, 4, &value, &shift);
	zassert_equal(shift, 0);
	zassert_equal(value, expected, "got %d, expected %d", value, expected);
}

static void check_angle(struct sensor_chan_spec chan, uint32_t counts_per_rev, int32_t raw)
{
	int64_t pos = (int64_t)raw % counts_per_rev;
	int64_t expected_udeg;
	int64_t actual_udeg;
	q31_t value;
	int8_t shift;

	if (pos < 0) {
		pos += counts_per_rev;
	}

	expected_udeg = pos * 360000000LL / counts_per_rev;

	decode_one(chan, 1, &value, &shift);
	zassert_equal(shift, 9);

	actual_udeg = ((int64_t)value * 1000000) >> (31 - shift);
	zassert_within(actual_udeg, expected_udeg, TOLERANCE_UDEG,
		       "raw %d, cpr %u: got %lld, expected %lld", raw, counts_per_rev, actual_udeg,
		       expected_udeg);
	/* The angle is truncated to the q31 resolution */
	zassert_equal(value, (q31_t)(((uint64_t)pos * 360U << 22) / counts_per_rev));
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_unknown_unit(struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -EINVAL);
}

ZTEST(pcnt_esp32_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(rotation(0), &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(count(2), &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

ZTEST(pcnt_esp32_decoder, test_encoder_count)
{
	check_count(count(0), 100);
	check_count(count(2), -4567);
	check_count(count(3), 0);

	edata.counts[POS_UNIT3] = INT32_MIN;
	check_count(count(3), INT32_MIN);
	edata.counts[POS_UNIT3] = INT32_MAX;
	check_count(count(3), INT32_MAX);
}

ZTEST(pcnt_esp32_decoder, test_rotation_without_counts_per_rev)
{
	/* Raw counts, like SENSOR_CHAN_ENCODER_COUNT */
	check_count(rotation(2), -4567);
	check_count(rotation(3), 0);
}

ZTEST(pcnt_esp32_decoder, test_rotation_angle)
{
	static const int32_t raws[] = {
		0, 1, 100, 399, 400, 401, 1234567, -1, -100, -400, INT32_MAX, INT32_MIN,
	};

	for (size_t i = 0; i < ARRAY_SIZE(raws); i++) {
		edata.counts[POS_UNIT0] = raws[i];
		check_angle(rotation(0), 400, raws[i]);
	}

	/* 100 of 400 counts: 90 degrees, the encoder count of the same unit stays raw */
	edata.counts[POS_UNIT0] = 100;
	check_angle(rotation(0), 400, 100);
	check_count(count(0), 100);

	/* Odd resolution */
	set_unit(POS_UNIT3, 3, -7, 3);
	check_angle(rotation(3), 3, -7);
}

ZTEST(pcnt_esp32_decoder, test_rotation_large_counts_per_rev)
{
	set_unit(POS_UNIT3, 3, 19999999, 20000000);
	check_angle(rotation(3), 20000000, 19999999);

	set_unit(POS_UNIT3, 3, INT32_MAX - 1, INT32_MAX);
	check_angle(rotation(3), INT32_MAX, INT32_MAX - 1);

	set_unit(POS_UNIT3, 3, INT32_MIN, INT32_MAX);
	check_angle(rotation(3), INT32_MAX, INT32_MIN);
}

ZTEST(pcnt_esp32_decoder, test_rotation_invalid_counts_per_rev)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	set_unit(POS_UNIT3, 3, 5, (uint32_t)INT32_MAX + 1U);

	zassert_equal(decoder->get_frame_count(buffer, rotation(3), &frame_count), -EINVAL);
	zassert_equal(decoder->decode(buffer, rotation(3), &fit, 1, &out), -EINVAL);

	check_count(count(3), 5);
}

ZTEST(pcnt_esp32_decoder, test_max_count_zero)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buffer, count(0), &fit, 0, &out), 0);
	zassert_equal(decoder->decode(buffer, count(0), &fit, 1, &out), 1);
	zassert_equal(out.readings[0].value, 100);
}

ZTEST(pcnt_esp32_decoder, test_unsupported)
{
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_PCNT_ESP32_UNIT(0), 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});

	check_unknown_unit(count(1));
	check_unknown_unit(rotation(7));
	check_unknown_unit(count(0x100));

	edata.num_units = 0;
	check_unknown_unit(count(0));
}

ZTEST(pcnt_esp32_decoder, test_no_trigger)
{
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(pcnt_esp32_decoder, NULL, setup, before, NULL, NULL);
