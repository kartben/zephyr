/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/afbr_s50.h>
#include <zephyr/ztest.h>

#include "afbr_s50_decoder.h"

#define NUM_PIXELS 32U

/* Allowed error of a decoded range, in micrometers */
#define TOLERANCE_UM 2

#define TIMESTAMP_NS 123456789012ULL

/* Invalid pixels: overflowing amplitude and saturated */
#define PIXEL_OVERFLOW  3U
#define PIXEL_SATURATED 17U

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(afbr_s50));

static const struct sensor_chan_spec chan_distance = {SENSOR_CHAN_DISTANCE, 0};
static const struct sensor_chan_spec chan_pixels = {SENSOR_CHAN_AFBR_S50_PIXELS, 0};

static struct afbr_s50_edata edata;

static uint8_t out_buf[sizeof(struct sensor_q31_data) +
		       (NUM_PIXELS - 1U) * sizeof(struct sensor_q31_sample_data)] __aligned(8);

/* Range values are Q9.22, in meters */
static int32_t um_to_q9_22(int64_t um)
{
	return (int32_t)(um * (1 << 22) / 1000000);
}

static int64_t q31_to_um(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Range of pixel n: 0.25 m + n * 12.5 cm */
static int64_t pixel_um(uint32_t n)
{
	return 250000 + (int64_t)n * 125000;
}

static bool pixel_is_valid(uint32_t n)
{
	return n != PIXEL_OVERFLOW && n != PIXEL_SATURATED;
}

static void check_range(q31_t value, int8_t shift, int64_t expected_um)
{
	int64_t actual = q31_to_um(value, shift);

	zassert_equal(shift, 9);
	zassert_within(actual, expected_um, TOLERANCE_UM, "got %lld um, expected %lld um", actual,
		       expected_um);
}

static void check_pixel(const struct sensor_q31_data *out, uint32_t reading, uint32_t pixel)
{
	zassert_equal(out->readings[reading].timestamp_delta, 0U);

	if (pixel_is_valid(pixel)) {
		check_range(out->readings[reading].value, out->shift, pixel_um(pixel));
	} else {
		zassert_equal(out->readings[reading].value, (q31_t)AFBR_PIXEL_INVALID_VALUE,
			      "pixel %u", pixel);
	}
}

static void afbr_s50_before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(&edata, 0, sizeof(edata));
	memset(out_buf, 0xAA, sizeof(out_buf));

	edata.header.timestamp = TIMESTAMP_NS;
	edata.header.channels = afbr_s50_encode_channel(SENSOR_CHAN_DISTANCE) |
				afbr_s50_encode_channel(SENSOR_CHAN_AFBR_S50_PIXELS);
	edata.payload.Bin.Range = um_to_q9_22(1234500);

	for (uint32_t i = 0U; i < NUM_PIXELS; i++) {
		edata.payload.Pixels[i].Range = um_to_q9_22(pixel_um(i));
		edata.payload.Pixels[i].Amplitude = 100U;
		edata.payload.Pixels[i].Status = PIXEL_OK;
	}

	edata.payload.Pixels[PIXEL_OVERFLOW].Amplitude = 0xFFFFU;
	edata.payload.Pixels[PIXEL_SATURATED].Status = PIXEL_SAT;
}

ZTEST(afbr_s50_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_distance, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	/* One reading per pixel */
	zassert_ok(decoder->get_size_info(chan_pixels, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	zassert_equal(base_size + (NUM_PIXELS - 1U) * frame_size, sizeof(out_buf));
}

ZTEST(afbr_s50_decoder, test_distance)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_distance, &frame_count));
	zassert_equal(frame_count, 1U);

	zassert_equal(decoder->decode(buffer, chan_distance, &fit, 0, out), 0);
	zassert_equal(fit, 0U);

	zassert_equal(decoder->decode(buffer, chan_distance, &fit, NUM_PIXELS, out), 1);
	zassert_equal(out->header.reading_count, 1U);
	zassert_equal(out->header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out->readings[0].timestamp_delta, 0U);
	check_range(out->readings[0].value, out->shift, 1234500);

	zassert_equal(decoder->decode(buffer, chan_distance, &fit, 1, out), 0);
}

ZTEST(afbr_s50_decoder, test_distance_limits)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint32_t fit = 0;

	/* Q9.22 values are copied unchanged, the maximum range included */
	edata.payload.Bin.Range = INT32_MAX;
	zassert_equal(decoder->decode(buffer, chan_distance, &fit, 1, out), 1);
	zassert_equal(out->readings[0].value, INT32_MAX);
	zassert_equal(out->shift, 9);

	fit = 0;
	edata.payload.Bin.Range = um_to_q9_22(-500000);
	zassert_equal(decoder->decode(buffer, chan_distance, &fit, 1, out), 1);
	check_range(out->readings[0].value, out->shift, -500000);
}

ZTEST(afbr_s50_decoder, test_pixels_all)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_pixels, &frame_count));
	zassert_equal(frame_count, NUM_PIXELS);

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, frame_count, out), NUM_PIXELS);
	zassert_equal(out->header.reading_count, NUM_PIXELS);
	zassert_equal(out->header.base_timestamp_ns, TIMESTAMP_NS);

	for (uint32_t i = 0U; i < NUM_PIXELS; i++) {
		check_pixel(out, i, i);
	}

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, frame_count, out), 0);
}

ZTEST(afbr_s50_decoder, test_pixels_one_by_one)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint32_t fit = 0;

	for (uint32_t i = 0U; i < NUM_PIXELS; i++) {
		zassert_equal(decoder->decode(buffer, chan_pixels, &fit, 1, out), 1, "pixel %u", i);
		zassert_equal(out->header.reading_count, 1U);
		zassert_equal(out->header.base_timestamp_ns, TIMESTAMP_NS);
		check_pixel(out, 0U, i);
	}

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, 1, out), 0);
}

ZTEST(afbr_s50_decoder, test_pixels_in_batches)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, 20, out), 20);
	for (uint32_t i = 0U; i < 20U; i++) {
		check_pixel(out, i, i);
	}

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, 20, out), NUM_PIXELS - 20U);
	zassert_equal(out->header.base_timestamp_ns, TIMESTAMP_NS);
	for (uint32_t i = 20U; i < NUM_PIXELS; i++) {
		check_pixel(out, i - 20U, i);
	}

	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, 20, out), 0);
}

ZTEST(afbr_s50_decoder, test_channel_absent)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	edata.header.channels = afbr_s50_encode_channel(SENSOR_CHAN_DISTANCE);

	zassert_ok(decoder->get_frame_count(buffer, chan_pixels, &frame_count));
	zassert_equal(frame_count, 0U);
	zassert_equal(decoder->decode(buffer, chan_pixels, &fit, NUM_PIXELS, out), -ENODATA);

	zassert_ok(decoder->get_frame_count(buffer, chan_distance, &frame_count));
	zassert_equal(frame_count, 1U);

	edata.header.channels = afbr_s50_encode_channel(SENSOR_CHAN_AFBR_S50_PIXELS);

	zassert_ok(decoder->get_frame_count(buffer, chan_distance, &frame_count));
	zassert_equal(frame_count, 0U);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_distance, &fit, 1, out), -ENODATA);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP,
		      "channel %u:%u", chan.chan_type, chan.chan_idx);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, out_buf), -ENOTSUP, "channel %u:%u",
		      chan.chan_type, chan.chan_idx);
}

ZTEST(afbr_s50_decoder, test_unsupported)
{
	static const uint16_t types[] = {
		SENSOR_CHAN_ACCEL_XYZ,
		SENSOR_CHAN_PROX,
		SENSOR_CHAN_PRIV_START,
	};

	for (size_t i = 0; i < ARRAY_SIZE(types); i++) {
		struct sensor_chan_spec chan = {types[i], 0};
		size_t base_size;
		size_t frame_size;

		zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP,
			      "channel %u", types[i]);
		check_unsupported(chan);
	}

	/* Only index 0 exists */
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_DISTANCE, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AFBR_S50_PIXELS, 1});
}

ZTEST(afbr_s50_decoder, test_has_trigger)
{
	const uint8_t *buffer = (const uint8_t *)&edata;

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	edata.header.events = afbr_s50_encode_event(SENSOR_TRIG_DATA_READY);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_THRESHOLD));
}

ZTEST_SUITE(afbr_s50_decoder, NULL, NULL, afbr_s50_before, NULL, NULL);
