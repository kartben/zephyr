/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/max30009.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "max30009.h"

#define TAG_BIOZ_I  0x1U
#define TAG_BIOZ_Q  0x2U
#define TAG_MARKER  0xEU
#define TAG_INVALID 0x0U

#define MAX_WORDS  100
#define LAST_TS_NS 1000000000ULL
#define PERIOD_NS  1000000U

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(max30009));

static const struct sensor_chan_spec chan_i = {(enum sensor_channel)SENSOR_CHAN_BIOZ_I, 0};
static const struct sensor_chan_spec chan_q = {(enum sensor_channel)SENSOR_CHAN_BIOZ_Q, 0};

static struct {
	struct max30009_fifo_data hdr;
	uint8_t words[MAX_WORDS * MAX30009_FIFO_BYTES_PER_SAMPLE];
} fifo;

BUILD_ASSERT(offsetof(__typeof__(fifo), words) == sizeof(struct max30009_fifo_data));

struct fifo_word {
	uint8_t tag;
	int32_t value;
};

/* BioZ I and Q samples interleaved with a marker and an invalid word */
static const struct fifo_word mixed[] = {
	{TAG_BIOZ_I, 100},    {TAG_BIOZ_Q, -100}, {TAG_MARKER, 0}, {TAG_BIOZ_I, -524288},
	{TAG_BIOZ_Q, 524287}, {TAG_INVALID, 5},   {TAG_BIOZ_I, 0}, {TAG_BIOZ_Q, -1},
};

static const int32_t mixed_i[] = {100, -524288, 0};
static const int32_t mixed_q[] = {-100, 524287, -1};

static void put_word(size_t idx, uint8_t tag, int32_t value)
{
	uint32_t word = FIELD_PREP(GENMASK(23, 20), tag) | ((uint32_t)value & GENMASK(19, 0));

	sys_put_be24(word, &fifo.words[idx * MAX30009_FIFO_BYTES_PER_SAMPLE]);
}

static void fill_fifo(const struct fifo_word *words, size_t num, uint32_t period_ns)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.timestamp = LAST_TS_NS;
	fifo.hdr.status1 = MAX30009_STATUS1_A_FULL_MSK;
	fifo.hdr.fifo_samples = num;
	fifo.hdr.fifo_byte_count = num * MAX30009_FIFO_BYTES_PER_SAMPLE;
	fifo.hdr.sample_set_size = MAX30009_FIFO_BYTES_PER_SAMPLE;
	fifo.hdr.sample_period_ns = period_ns;

	for (size_t i = 0; i < num; i++) {
		put_word(i, words[i].tag, words[i].value);
	}
}

static uint64_t reading_ts(const struct sensor_q31_data *out, int idx)
{
	const struct sensor_q31_sample_data *readings = out->readings;

	return out->header.base_timestamp_ns + readings[idx].timestamp_delta;
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
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(max30009_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_i, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(chan_q, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
}

static void check_all(struct sensor_chan_spec chan, const int32_t *expected, int num)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[ARRAY_SIZE(mixed_i) - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, num);

	zassert_equal(decoder->decode(buffer, chan, &fit, num, &out), num);
	zassert_equal(out.data.header.reading_count, num);
	zassert_equal(out.data.header.base_timestamp_ns, LAST_TS_NS - (num - 1) * PERIOD_NS);
	zassert_equal(out.data.shift, 0);

	for (int i = 0; i < num; i++) {
		/* Raw ADC counts with a shift of 0 */
		zassert_equal(readings[i].value, expected[i], "reading %d: %d", i,
			      readings[i].value);
		zassert_equal(readings[i].timestamp_delta, i * PERIOD_NS);
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, num, &out), 0);
}

static void check_one_by_one(struct sensor_chan_spec chan, const int32_t *expected, int num)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_q31_data out;
	uint32_t fit = 0;

	for (int i = 0; i < num; i++) {
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.shift, 0);
		zassert_equal(out.readings[0].value, expected[i]);
		zassert_equal(reading_ts(&out, 0), LAST_TS_NS - (num - 1 - i) * PERIOD_NS);
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

ZTEST(max30009_decoder, test_fifo)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	fill_fifo(mixed, ARRAY_SIZE(mixed), PERIOD_NS);

	check_all(chan_i, mixed_i, ARRAY_SIZE(mixed_i));
	check_all(chan_q, mixed_q, ARRAY_SIZE(mixed_q));
	check_one_by_one(chan_i, mixed_i, ARRAY_SIZE(mixed_i));
	check_one_by_one(chan_q, mixed_q, ARRAY_SIZE(mixed_q));

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	fifo.hdr.status1 = 0;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
	check_unsupported(
		buffer, (struct sensor_chan_spec){(enum sensor_channel)SENSOR_CHAN_BIOZ_Q + 1, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){chan_i.chan_type, 1});
}

ZTEST(max30009_decoder, test_fifo_no_period)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[ARRAY_SIZE(mixed_q) - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint32_t fit = 0;

	/* Unknown BioZ ADC clock: every reading gets the FIFO timestamp */
	fill_fifo(mixed, ARRAY_SIZE(mixed), 0);

	zassert_equal(decoder->decode(buffer, chan_q, &fit, ARRAY_SIZE(mixed_q), &out),
		      ARRAY_SIZE(mixed_q));
	for (int i = 0; i < ARRAY_SIZE(mixed_q); i++) {
		zassert_equal(reading_ts(&out.data, i), LAST_TS_NS);
		zassert_equal(readings[i].value, mixed_q[i]);
	}
}

ZTEST(max30009_decoder, test_fifo_long_period)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	/* Slowest rate: BioZ ADC OSR of 1024 at 16 kHz */
	const uint32_t period_ns = 64000000U;
	/* Late enough for the oldest reading to have a positive timestamp */
	const uint64_t last_ts_ns = 100000000000ULL;
	/* Readings 0 to 67 are at most UINT32_MAX ns after reading 0 */
	const int batches[] = {68, MAX_WORDS - 68};
	static struct fifo_word words[MAX_WORDS];
	static struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[MAX_WORDS - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;
	int decoded = 0;

	for (int i = 0; i < MAX_WORDS; i++) {
		words[i] = (struct fifo_word){TAG_BIOZ_I, i - 50};
	}
	fill_fifo(words, MAX_WORDS, period_ns);
	fifo.hdr.timestamp = last_ts_ns;

	zassert_ok(decoder->get_frame_count(buffer, chan_i, &frame_count));
	zassert_equal(frame_count, MAX_WORDS);

	/* The readings span more than UINT32_MAX ns, so two calls are needed */
	for (int b = 0; b < ARRAY_SIZE(batches); b++) {
		zassert_equal(decoder->decode(buffer, chan_i, &fit, MAX_WORDS, &out), batches[b],
			      "batch %d", b);
		zassert_equal(out.data.header.reading_count, batches[b]);
		for (int i = 0; i < batches[b]; i++) {
			int idx = decoded + i;

			zassert_equal(readings[i].value, idx - 50);
			zassert_equal(reading_ts(&out.data, i),
				      last_ts_ns - (uint64_t)(MAX_WORDS - 1 - idx) * period_ns,
				      "reading %d", idx);
		}
		decoded += batches[b];
	}

	zassert_equal(decoder->decode(buffer, chan_i, &fit, MAX_WORDS, &out), 0);
}

ZTEST(max30009_decoder, test_fifo_partial_word)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(mixed, 4, PERIOD_NS);
	/* Two bytes of a BioZ I word end the data and are ignored */
	put_word(4, TAG_BIOZ_I, 7);
	fifo.hdr.fifo_byte_count = 4 * MAX30009_FIFO_BYTES_PER_SAMPLE + 2;

	zassert_ok(decoder->get_frame_count(buffer, chan_i, &frame_count));
	zassert_equal(frame_count, 2);

	for (int i = 0; i < 2; i++) {
		zassert_equal(decoder->decode(buffer, chan_i, &fit, 1, &out), 1);
		zassert_equal(out.readings[0].value, mixed_i[i]);
		zassert_equal(reading_ts(&out, 0), LAST_TS_NS - (1 - i) * PERIOD_NS);
	}
	zassert_equal(decoder->decode(buffer, chan_i, &fit, 1, &out), 0);
}

ZTEST(max30009_decoder, test_fifo_other_channel)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	static const struct fifo_word words[] = {
		{TAG_MARKER, 0}, {TAG_BIOZ_I, 1}, {TAG_INVALID, 2}, {TAG_BIOZ_I, 3}};
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* FIFO data without any BioZ Q sample */
	fill_fifo(words, ARRAY_SIZE(words), PERIOD_NS);

	zassert_ok(decoder->get_frame_count(buffer, chan_q, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_q, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_q, &fit, 0, &out), -ENODATA);

	zassert_ok(decoder->get_frame_count(buffer, chan_i, &frame_count));
	zassert_equal(frame_count, 2);
	zassert_equal(decoder->decode(buffer, chan_i, &fit, 1, &out), 1);
	zassert_equal(out.readings[0].value, 1);
	zassert_equal(reading_ts(&out, 0), LAST_TS_NS - PERIOD_NS);
}

ZTEST(max30009_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.timestamp = LAST_TS_NS;
	fifo.hdr.status1 = MAX30009_STATUS1_A_FULL_MSK;

	zassert_ok(decoder->get_frame_count(buffer, chan_i, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_ok(decoder->get_frame_count(buffer, chan_q, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_i, &fit, 1, &out), 0);
	zassert_equal(decoder->decode(buffer, chan_q, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0});
}

ZTEST_SUITE(max30009_decoder, NULL, NULL, NULL, NULL, NULL);
