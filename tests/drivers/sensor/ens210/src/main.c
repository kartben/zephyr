/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "ens210.h"

/* Allowed error of a decoded value, in micro-degC or micro-%RH */
#define TOLERANCE_MICRO 100

#define TIMESTAMP_NS 0x123456789abcULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(ens210));

static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};
static const struct sensor_chan_spec chan_hum = {SENSOR_CHAN_HUMIDITY, 0};

static struct ens210_encoded_data edata;
static const uint8_t *const buffer = (const uint8_t *)&edata;

static struct {
	struct sensor_q31_data data;
	struct sensor_q31_sample_data extra[3];
} out;

static void fill_buffer(uint16_t t_val, bool t_valid, uint16_t h_val, bool h_valid)
{
	memset(&edata, 0, sizeof(edata));
	edata.header.base_timestamp_ns = TIMESTAMP_NS;
	edata.header.reading_count = 1U;
	edata.temp.val = sys_cpu_to_le16(t_val);
	edata.temp.valid = t_valid ? 1U : 0U;
	edata.humidity.val = sys_cpu_to_le16(h_val);
	edata.humidity.valid = h_valid ? 1U : 0U;
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* T_VAL is in 1/64 K */
static int64_t temp_to_micro(uint16_t t_val)
{
	return (int64_t)t_val * 1000000 / 64 - 273150000;
}

/* H_VAL is in 1/512 %RH */
static int64_t hum_to_micro(uint16_t h_val)
{
	return (int64_t)h_val * 1000000 / 512;
}

/* Decode the only reading of the channel, one reading at a time */
static void check_decode(struct sensor_chan_spec chan, int64_t expected)
{
	uint16_t frame_count;
	uint32_t fit = 0;
	int64_t actual;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.data.readings[0].timestamp_delta, 0);
	zassert_equal(out.data.shift, 16);

	actual = q31_to_micro(out.data.readings[0].value, out.data.shift);
	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

/* The buffer holds a frame, but no reading of the channel */
static void check_no_reading(struct sensor_chan_spec chan)
{
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 4, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENODATA);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), -ENOTSUP);

	/* Also with the iterator past the reading of a supported channel */
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(ens210_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(chan_hum, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

ZTEST(ens210_decoder, test_temperature)
{
	/* -273.15, about -273.13, 0.00, 25.02, 750.83 degC */
	static const uint16_t t_vals[] = {0U, 1U, 17482U, 19083U, 0xffffU};

	ARRAY_FOR_EACH(t_vals, i) {
		fill_buffer(t_vals[i], true, 25600U, true);
		check_decode(chan_temp, temp_to_micro(t_vals[i]));
	}
}

ZTEST(ens210_decoder, test_humidity)
{
	/* 0, about 0.002, 50, 100, 127.998 %RH */
	static const uint16_t h_vals[] = {0U, 1U, 25600U, 51200U, 0xffffU};

	ARRAY_FOR_EACH(h_vals, i) {
		fill_buffer(19083U, true, h_vals[i], true);
		check_decode(chan_hum, hum_to_micro(h_vals[i]));
	}
}

ZTEST(ens210_decoder, test_decode_all)
{
	uint32_t fit = 0;

	fill_buffer(19083U, true, 25600U, true);

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 0, &out), 0);
	zassert_equal(fit, 0);

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 4, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.data.readings[0].timestamp_delta, 0);
	zassert_within(q31_to_micro(out.data.readings[0].value, out.data.shift),
		       temp_to_micro(19083U), TOLERANCE_MICRO);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 4, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_hum, &fit, 4, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_within(q31_to_micro(out.data.readings[0].value, out.data.shift),
		       hum_to_micro(25600U), TOLERANCE_MICRO);
	zassert_equal(decoder->decode(buffer, chan_hum, &fit, 4, &out), 0);
}

ZTEST(ens210_decoder, test_invalid_value)
{
	/* A channel without a valid measurement, for example a disabled one, has no reading */
	fill_buffer(19083U, false, 25600U, true);
	check_no_reading(chan_temp);
	check_decode(chan_hum, hum_to_micro(25600U));

	fill_buffer(19083U, true, 25600U, false);
	check_no_reading(chan_hum);
	check_decode(chan_temp, temp_to_micro(19083U));
}

ZTEST(ens210_decoder, test_unsupported)
{
	fill_buffer(19083U, true, 25600U, true);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_HUMIDITY, 1});
}

ZTEST(ens210_decoder, test_has_trigger)
{
	/* The driver does not support triggers */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(ens210_decoder, NULL, NULL, NULL, NULL, NULL);
