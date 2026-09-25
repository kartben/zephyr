/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "bme680.h"

#define TIMESTAMP_NS 123456789012ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bme680));

struct chan_desc {
	enum sensor_channel chan;
	int8_t shift;
	/* Value of one LSB of the compensated value in micro-units of the channel */
	int64_t micro_per_lsb;
	/* Allowed error of a decoded value in micro-units */
	int64_t tolerance;
};

static const struct chan_desc chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, BME680_TEMP_SHIFT, 10000, 2};
static const struct chan_desc chan_press = {SENSOR_CHAN_PRESS, BME680_PRESS_SHIFT, 1000, 2};
static const struct chan_desc chan_hum = {SENSOR_CHAN_HUMIDITY, BME680_HUM_SHIFT, 1000, 2};
static const struct chan_desc chan_gas = {SENSOR_CHAN_GAS_RES, BME680_GAS_SHIFT, 1000000, 10000};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static q31_t raw_to_q31(const struct chan_desc *desc, int64_t raw)
{
	int64_t value = raw * ((int64_t)1 << (31 - desc->shift)) / (1000000 / desc->micro_per_lsb);

	return (q31_t)CLAMP(value, INT32_MIN, INT32_MAX);
}

static struct bme680_encoded_data make_edata(int32_t temp, uint32_t press, uint32_t hum,
					     uint32_t gas)
{
	return (struct bme680_encoded_data){
		.header.timestamp = TIMESTAMP_NS,
		.has_temp = 1,
		.has_press = 1,
		.has_humidity = 1,
		.has_gas = 1,
		.reading = {
			.comp_temp = temp,
			.comp_press = press,
			.comp_humidity = hum,
			.comp_gas = gas,
		},
	};
}

static void check_reading(const struct sensor_q31_data *out, const struct chan_desc *desc,
			  int64_t raw)
{
	int64_t expected = raw * desc->micro_per_lsb;
	int64_t actual;

	zassert_equal(out->header.reading_count, 1);
	zassert_equal(out->header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out->readings[0].timestamp_delta, 0);
	zassert_equal(out->shift, desc->shift, "chan %d: shift %d", desc->chan, out->shift);

	actual = q31_to_micro(out->readings[0].value, out->shift);
	zassert_within(actual, expected, desc->tolerance, "chan %d: got %lld, expected %lld",
		       desc->chan, actual, expected);

	/* Exact value, truncated toward zero */
	zassert_equal(out->readings[0].value, raw_to_q31(desc, raw));
}

/* Decode a channel carrying one reading, in one call and with max_count = 1 */
static void check_chan(const uint8_t *buffer, const struct chan_desc *desc, int64_t raw)
{
	const struct sensor_chan_spec chan = {desc->chan, 0};
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[3];
	} out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 4, &out), 1);
	check_reading(&out.data, desc, raw);
	zassert_equal(decoder->decode(buffer, chan, &fit, 4, &out), 0);

	fit = 0;
	memset(&out, 0, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	check_reading(&out.data, desc, raw);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

/* Check that a failed decode() left the output and the iterator untouched */
static void check_untouched(const struct sensor_q31_data *out, uint32_t fit)
{
	struct sensor_q31_data pattern;

	memset(&pattern, 0xa5, sizeof(pattern));
	zassert_mem_equal(out, &pattern, sizeof(pattern));
	zassert_equal(fit, 0);
}

static void check_no_data(const uint8_t *buffer, const struct chan_desc *desc)
{
	const struct sensor_chan_spec chan = {desc->chan, 0};
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 0);

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
	check_untouched(&out, fit);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);

	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
	check_untouched(&out, fit);
}

ZTEST(bme680_decoder, test_size_info)
{
	const enum sensor_channel chans[] = {
		SENSOR_CHAN_AMBIENT_TEMP,
		SENSOR_CHAN_PRESS,
		SENSOR_CHAN_HUMIDITY,
		SENSOR_CHAN_GAS_RES,
	};
	size_t base_size;
	size_t frame_size;

	ARRAY_FOR_EACH(chans, i) {
		zassert_ok(decoder->get_size_info((struct sensor_chan_spec){chans[i], 0},
						  &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(bme680_decoder, test_all_channels)
{
	/* 23.45 degC, 101.325 kPa, 45.678 %RH, 123456 ohm */
	struct bme680_encoded_data edata = make_edata(2345, 101325, 45678, 123456);
	const uint8_t *buffer = (const uint8_t *)&edata;

	check_chan(buffer, &chan_temp, 2345);
	check_chan(buffer, &chan_press, 101325);
	check_chan(buffer, &chan_hum, 45678);
	check_chan(buffer, &chan_gas, 123456);
}

ZTEST(bme680_decoder, test_limits)
{
	/* -40.00 degC, 30 kPa, 0 and 100 %RH, 1 ohm */
	struct bme680_encoded_data edata = make_edata(-4000, 30000, 0, 1);
	const uint8_t *buffer = (const uint8_t *)&edata;

	check_chan(buffer, &chan_temp, -4000);
	check_chan(buffer, &chan_press, 30000);
	check_chan(buffer, &chan_hum, 0);
	check_chan(buffer, &chan_gas, 1);

	/* 85.00 degC, 110 kPa, 100 %RH, 16 Mohm */
	edata = make_edata(8500, 110000, 100000, 16000000);
	check_chan(buffer, &chan_temp, 8500);
	check_chan(buffer, &chan_press, 110000);
	check_chan(buffer, &chan_hum, 100000);
	check_chan(buffer, &chan_gas, 16000000);
}

ZTEST(bme680_decoder, test_saturation)
{
	struct bme680_encoded_data edata = make_edata(0, UINT32_MAX, UINT32_MAX, 20000000);
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	uint32_t fit;

	/* Gas resistance of 2^24 ohm or more exceeds the range of its shift */
	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GAS_RES, 0},
				      &fit, 1, &out),
		      1);
	zassert_equal(out.readings[0].resistance, INT32_MAX);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0}, &fit,
				      1, &out),
		      1);
	zassert_equal(out.readings[0].pressure, INT32_MAX);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_HUMIDITY, 0},
				      &fit, 1, &out),
		      1);
	zassert_equal(out.readings[0].humidity, INT32_MAX);
}

ZTEST(bme680_decoder, test_missing_channels)
{
	struct bme680_encoded_data edata = {
		.header.timestamp = TIMESTAMP_NS,
		.has_press = 1,
		.reading.comp_press = 95000,
	};
	const uint8_t *buffer = (const uint8_t *)&edata;

	check_chan(buffer, &chan_press, 95000);
	check_no_data(buffer, &chan_temp);
	check_no_data(buffer, &chan_hum);
	check_no_data(buffer, &chan_gas);

	/* A read of an unsupported channel completes without any channel */
	edata.has_press = 0;
	check_no_data(buffer, &chan_press);
}

ZTEST(bme680_decoder, test_unaligned)
{
	struct bme680_encoded_data edata = make_edata(-1234, 99999, 12345, 54321);
	uint8_t storage[sizeof(edata) + 1];
	const uint8_t *buffer = &storage[1];

	memcpy(&storage[1], &edata, sizeof(edata));

	check_chan(buffer, &chan_temp, -1234);
	check_chan(buffer, &chan_press, 99999);
	check_chan(buffer, &chan_hum, 12345);
	check_chan(buffer, &chan_gas, 54321);
}

ZTEST(bme680_decoder, test_max_count_zero)
{
	struct bme680_encoded_data edata = make_edata(2345, 101325, 45678, 123456);
	const uint8_t *buffer = (const uint8_t *)&edata;
	const struct sensor_chan_spec chan = {SENSOR_CHAN_AMBIENT_TEMP, 0};
	struct sensor_q31_data out;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buffer, chan, &fit, 0, &out), 0);
	zassert_equal(fit, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	check_reading(&out, &chan_temp, 2345);
}

ZTEST(bme680_decoder, test_unsupported)
{
	struct bme680_encoded_data edata = make_edata(2345, 101325, 45678, 123456);
	const uint8_t *buffer = (const uint8_t *)&edata;

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_VOC, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GAS_RES, 1});

	/* Reads are never streamed, so no buffer carries a trigger */
	zassert_is_null(decoder->has_trigger);
}

ZTEST_SUITE(bme680_decoder, NULL, NULL, NULL, NULL, NULL);
