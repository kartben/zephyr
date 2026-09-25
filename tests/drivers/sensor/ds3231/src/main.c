/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "ds3231.h"

/* Allowed error of a decoded value, in milli-degC */
#define TOLERANCE_MDEGC 1

#define TIMESTAMP_NS 123456789012ULL

static const struct sensor_decoder_api *decoder =
	SENSOR_DECODER_DT_GET(DT_NODELABEL(ds3231_sensor));

static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};

static struct sensor_ds3231_edata edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static int64_t q31_to_milli(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000) >> (31 - shift);
}

static void fill_edata(uint16_t raw_temp)
{
	memset(&edata, 0xa5, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.raw_temp = raw_temp;
}

static void check_reading(const struct sensor_q31_data *out, int32_t expected_mdegc)
{
	int64_t actual = q31_to_milli(out->readings[0].temperature, out->shift);

	zassert_equal(out->header.reading_count, 1);
	zassert_equal(out->shift, 7);
	zassert_equal(out->header.base_timestamp_ns + out->readings[0].timestamp_delta,
		      TIMESTAMP_NS);
	zassert_within(actual, expected_mdegc, TOLERANCE_MDEGC, "got %lld, expected %d", actual,
		       expected_mdegc);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(ds3231_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

ZTEST(ds3231_decoder, test_values)
{
	/* 10-bit two's complement, 0.25 degC/LSB */
	static const struct {
		uint16_t raw;
		int32_t mdegc;
	} cases[] = {
		{0x000, 0},      {0x001, 250},     {0x064, 25000},  {0x065, 25250},  {0x080, 32000},
		{0x1ff, 127750}, {0x200, -128000}, {0x39c, -25000}, {0x39d, -24750}, {0x3ff, -250},
	};

	for (size_t i = 0; i < ARRAY_SIZE(cases); i++) {
		struct sensor_q31_data out;
		uint32_t fit = 0;

		fill_edata(cases[i].raw);

		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
		check_reading(&out, cases[i].mdegc);
	}
}

ZTEST(ds3231_decoder, test_decode)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_edata(0x065);

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, 1);

	/* One by one */
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
	check_reading(&out, 25250);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);

	/* All readings in one call */
	fit = 0;
	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 8, &out), 1);
	check_reading(&out, 25250);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 8, &out), 0);

	/* No room for a reading */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 0, &out), 0);
	zassert_equal(fit, 0);
}

ZTEST(ds3231_decoder, test_unsupported)
{
	fill_edata(0x065);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
}

ZTEST(ds3231_decoder, test_no_trigger)
{
	/* One-shot reads only: the decoder reports no trigger */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(ds3231_decoder, NULL, NULL, NULL, NULL, NULL);
