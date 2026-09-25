/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define NUM_FRAMES 4
#define FRAME_SIZE 6
#define PERIOD_NS  SENSOR_ODR_MHZ_TO_PERIOD_NS(100000)
#define LAST_TS_NS 1000000000ULL
#define TEST_SHIFT 5
#define TEST_SCALE SENSOR_Q31_SCALE(SENSOR_G, 256 * 1000000LL, TEST_SHIFT)

/* 12-bit little-endian samples: frame i holds (i, -i, 800 - i) */
static uint8_t raw_frames[NUM_FRAMES * FRAME_SIZE];

struct test_decode_ctx {
	int calls;
	int fail_at;
	int drop_at;
};

static struct test_decode_ctx ctx;

static int test_decode_frame(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			     const void *user_data, struct sensor_frame_reading *reading)
{
	struct test_decode_ctx *c = (struct test_decode_ctx *)user_data;

	if (chan_spec.chan_type != SENSOR_CHAN_ACCEL_XYZ &&
	    chan_spec.chan_type != SENSOR_CHAN_ACCEL_Y) {
		return -ENOTSUP;
	}

	if (reading == NULL) {
		return 1;
	}

	if (c->calls++ == c->fail_at) {
		return -EIO;
	}

	if (c->calls - 1 == c->drop_at) {
		return -ENODATA;
	}

	if (chan_spec.chan_type == SENSOR_CHAN_ACCEL_XYZ) {
		for (int i = 0; i < 3; i++) {
			reading->values[i] =
				sensor_raw_to_q31(sys_get_le16(&frame[i * 2]), 12, TEST_SCALE);
		}
	} else {
		reading->values[0] = sensor_raw_to_q31(sys_get_le16(&frame[2]), 12, TEST_SCALE);
	}

	return 1;
}

static const struct sensor_raw_frames frames = {
	.frames = raw_frames,
	.size = sizeof(raw_frames),
	.frame_size = FRAME_SIZE,
	.decode_frame = test_decode_frame,
	.user_data = &ctx,
	.timestamp_ns = LAST_TS_NS,
	.period_ns = PERIOD_NS,
	.shift = TEST_SHIFT,
};

/* readings[] is declared with one element and extended past the end of the structure */
static const struct sensor_three_axis_sample_data *
three_axis_reading(const struct sensor_three_axis_data *data, int idx)
{
	return (const struct sensor_three_axis_sample_data *)((const uint8_t *)data->readings +
							      idx * sizeof(data->readings[0]));
}

static const struct sensor_q31_sample_data *q31_reading(const struct sensor_q31_data *data, int idx)
{
	return (const struct sensor_q31_sample_data *)((const uint8_t *)data->readings +
						       idx * sizeof(data->readings[0]));
}

static uint64_t three_axis_ts(const struct sensor_three_axis_data *data, int idx)
{
	return data->header.base_timestamp_ns + three_axis_reading(data, idx)->timestamp_delta;
}

static uint64_t q31_ts(const struct sensor_q31_data *data, int idx)
{
	return data->header.base_timestamp_ns + q31_reading(data, idx)->timestamp_delta;
}

static uint64_t frame_ts(int frame)
{
	return LAST_TS_NS - (NUM_FRAMES - 1 - frame) * PERIOD_NS;
}

static void check_three_axis_reading(const struct sensor_three_axis_data *data, int idx, int frame)
{
	const struct sensor_three_axis_sample_data *reading = three_axis_reading(data, idx);

	zassert_equal(three_axis_ts(data, idx), frame_ts(frame));
	zassert_equal(reading->x, frame * TEST_SCALE);
	zassert_equal(reading->y, -frame * TEST_SCALE);
	zassert_equal(reading->z, (800 - frame) * TEST_SCALE);
}

static void *setup(void)
{
	for (int i = 0; i < NUM_FRAMES; i++) {
		sys_put_le16((uint16_t)i & 0xfffU, &raw_frames[i * FRAME_SIZE]);
		sys_put_le16((uint16_t)-i & 0xfffU, &raw_frames[i * FRAME_SIZE + 2]);
		sys_put_le16((uint16_t)(800 - i) & 0xfffU, &raw_frames[i * FRAME_SIZE + 4]);
	}

	return NULL;
}

static void before(void *fixture)
{
	ARG_UNUSED(fixture);

	ctx.calls = 0;
	ctx.fail_at = -1;
	ctx.drop_at = -1;
}

ZTEST_SUITE(sensor_decoder_helpers, NULL, setup, before, NULL, NULL);

ZTEST(sensor_decoder_helpers, test_odr_period)
{
	zassert_equal(SENSOR_ODR_MHZ_TO_PERIOD_NS(400000), 2500000);
	zassert_equal(SENSOR_ODR_MHZ_TO_PERIOD_NS(62500), 16000000);
	zassert_equal(SENSOR_ODR_MHZ_TO_PERIOD_NS(12500), 80000000);
	zassert_equal(SENSOR_ODR_MHZ_TO_PERIOD_NS(3906), 256016385);
	zassert_equal(SENSOR_ODR_MHZ_TO_PERIOD_NS(125), 8000000000ULL);
}

ZTEST(sensor_decoder_helpers, test_q31_scale)
{
	/* 1 LSB = 1/256 g, shift 5: 2^31 / 2^5 / 256 * 9.80665 */
	zassert_equal(SENSOR_Q31_SCALE(SENSOR_G, 256 * 1000000LL, 5), 2570754);
	zassert_equal(SENSOR_Q31_SCALE(SENSOR_G, 256 * 1000000LL, 8), 321344);
	zassert_equal(SENSOR_Q31_SCALE(1, 2, 0), 1073741824);
}

ZTEST(sensor_decoder_helpers, test_raw_to_q31)
{
	zassert_equal(sensor_raw_to_q31(0x7ff, 12, 3), 2047 * 3);
	zassert_equal(sensor_raw_to_q31(0x800, 12, 3), -2048 * 3);
	zassert_equal(sensor_raw_to_q31(0xf800, 12, 3), -2048 * 3, "upper bits must be ignored");
	zassert_equal(sensor_raw_to_q31(0xffff, 16, 1), -1);
	zassert_equal(sensor_raw_to_q31(0x0, 16, 1), 0);
	zassert_equal(sensor_raw_to_q31(0x7fff, 16, 0x10000000), INT32_MAX, "must saturate");
	zassert_equal(sensor_raw_to_q31(0x8000, 16, 0x10000000), INT32_MIN, "must saturate");
}

ZTEST(sensor_decoder_helpers, test_raw_to_q31_ratio)
{
	/* 1 LSB = 1/7500 unit, shift 11: 2^20 / 7500 per LSB, not an integer */
	zassert_equal(sensor_raw_to_q31_ratio(7500, 24, 1, 7500, 11), 1 << 20);
	zassert_equal(sensor_raw_to_q31_ratio(0xffffff, 24, 1, 7500, 11), -139);
	zassert_equal(sensor_raw_to_q31_ratio(0x7fffff, 24, 1, 7500, 11), 1172812263);
	/* 1 LSB = 1 unit, shift 0: 1 does not fit in q31 */
	zassert_equal(sensor_raw_to_q31_ratio(1, 8, 1, 1, 0), INT32_MAX);
	zassert_equal(sensor_raw_to_q31_ratio(0xff, 8, 1, 1, 0), INT32_MIN);
	zassert_equal(sensor_raw_to_q31_ratio(0x7fffffff, 32, 0x7fffffff, 1, 31), INT32_MAX);
}

ZTEST(sensor_decoder_helpers, test_decode_all_frames)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(sensor_raw_frames_count(&frames, chan, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(sensor_decode_frames(&frames, chan, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES);
	zassert_equal(out.data.header.base_timestamp_ns, frame_ts(0));
	zassert_equal(out.data.shift, TEST_SHIFT);

	for (int i = 0; i < NUM_FRAMES; i++) {
		check_three_axis_reading(&out.data, i, i);
	}

	zassert_equal(sensor_decode_frames(&frames, chan, &fit, NUM_FRAMES, &out), 0);
}

ZTEST(sensor_decoder_helpers, test_decode_resume)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint32_t fit = 0;

	for (int i = 0; i < NUM_FRAMES; i += 2) {
		zassert_equal(sensor_decode_frames(&frames, chan, &fit, 2, &out), 2);
		zassert_equal(out.data.header.reading_count, 2);
		check_three_axis_reading(&out.data, 0, i);
		check_three_axis_reading(&out.data, 1, i + 1);
	}

	zassert_equal(sensor_decode_frames(&frames, chan, &fit, 2, &out), 0);
}

ZTEST(sensor_decoder_helpers, test_decode_single_axis)
{
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_Y, 0};
	uint32_t fit = 0;

	zassert_equal(sensor_decode_frames(&frames, chan, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.shift, TEST_SHIFT);

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(q31_ts(&out.data, i), frame_ts(i));
		zassert_equal(q31_reading(&out.data, i)->value, -i * TEST_SCALE);
	}
}

ZTEST(sensor_decoder_helpers, test_decode_errors)
{
	struct sensor_three_axis_data out;
	struct sensor_raw_frames bad = frames;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(sensor_decode_frames(&frames, (struct sensor_chan_spec){SENSOR_CHAN_PROX, 0},
					   &fit, 1, &out),
		      -ENOTSUP);
	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0}, &fit,
					   1, &out),
		      -ENOTSUP);
	zassert_equal(sensor_raw_frames_count(&frames,
					      (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0},
					      &frame_count),
		      -ENOTSUP);
	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					   &fit, 1, &out),
		      -ENOTSUP);
	zassert_equal(fit, 0);

	bad.num_values = 2;
	zassert_equal(sensor_decode_frames(&bad,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 1, &out),
		      -EINVAL);

	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 0, &out),
		      0);

	ctx.fail_at = 1;
	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 1, &out),
		      1);
	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 1, &out),
		      -EIO);
	ctx.fail_at = -1;
	zassert_equal(sensor_decode_frames(&frames,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 1, &out),
		      1);
	check_three_axis_reading(&out, 0, 1);
}

ZTEST(sensor_decoder_helpers, test_decode_dropped_reading)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint32_t fit = 0;

	/* The second reading is dropped, the others keep their timestamps */
	ctx.drop_at = 1;
	zassert_equal(sensor_decode_frames(&frames, chan, &fit, NUM_FRAMES, &out), NUM_FRAMES - 1);
	check_three_axis_reading(&out.data, 0, 0);
	check_three_axis_reading(&out.data, 1, 2);
	check_three_axis_reading(&out.data, 2, 3);
	zassert_equal(sensor_decode_frames(&frames, chan, &fit, NUM_FRAMES, &out), 0);
}

ZTEST(sensor_decoder_helpers, test_decode_single_sample)
{
	struct sensor_raw_frames single = frames;
	struct sensor_three_axis_data out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint32_t fit = 0;

	single.frames = &raw_frames[3 * FRAME_SIZE];
	single.size = FRAME_SIZE;
	single.period_ns = 0;

	zassert_equal(sensor_decode_frames(&single, chan, &fit, 4, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, LAST_TS_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.readings[0].x, 3 * TEST_SCALE);
	zassert_equal(sensor_decode_frames(&single, chan, &fit, 4, &out), 0);

	single.size = FRAME_SIZE - 1;
	fit = 0;
	zassert_equal(sensor_decode_frames(&single, chan, &fit, 4, &out), 0,
		      "a partial frame must be ignored");
}

ZTEST(sensor_decoder_helpers, test_decode_rebase)
{
	struct sensor_raw_frames slow = frames;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint32_t fit = 0;

	/* 5 s apart: two readings do not fit in one batch of uint32_t deltas */
	slow.period_ns = 5000000000ULL;
	slow.timestamp_ns = 100000000000ULL;

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(sensor_decode_frames(&slow, chan, &fit, NUM_FRAMES, &out), 1);
		zassert_equal(three_axis_ts(&out.data, 0),
			      slow.timestamp_ns - (NUM_FRAMES - 1 - i) * slow.period_ns);
		zassert_equal(out.data.readings[0].x, i * TEST_SCALE);
	}
	zassert_equal(sensor_decode_frames(&slow, chan, &fit, NUM_FRAMES, &out), 0);
}

ZTEST(sensor_decoder_helpers, test_decode_period_den)
{
	struct sensor_raw_frames frac = frames;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_XYZ, 0};
	uint32_t fit = 0;

	/* 1/3 ms period */
	frac.period_ns = 1000000;
	frac.period_den = 3;

	zassert_equal(sensor_decode_frames(&frac, chan, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(three_axis_ts(&out.data, 0), LAST_TS_NS - 1000000);
	zassert_equal(three_axis_ts(&out.data, 1), LAST_TS_NS - 666666);
	zassert_equal(three_axis_ts(&out.data, 2), LAST_TS_NS - 333333);
	zassert_equal(three_axis_ts(&out.data, 3), LAST_TS_NS);
}

/*
 * Tagged, variable-size frames: byte 0 is the tag, byte 1 the payload length. Tag 'A' frames
 * carry one 16-bit value per reading, tag 'B' frames are skipped, tag 0 ends the data.
 */
static const uint8_t tagged[] = {
	'A',  2,    0x01, 0x00, 'B',  1,    0xff, 'A', 4, 0x02, 0x00,
	0x03, 0x00, 'A',  2,    0x04, 0x00, 0,    0,   0, 0,
};

static int tagged_len(const uint8_t *frame, size_t remaining, const void *user_data)
{
	ARG_UNUSED(user_data);

	if (remaining < 2U || frame[0] == 0U) {
		return 0;
	}

	return 2 + frame[1];
}

static int tagged_decode(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			 const void *user_data, struct sensor_frame_reading *reading)
{
	ARG_UNUSED(user_data);

	if (chan_spec.chan_type != SENSOR_CHAN_DIE_TEMP) {
		return -ENOTSUP;
	}

	if (frame[0] != 'A') {
		return 0;
	}

	if (reading != NULL) {
		reading->values[0] = sys_get_le16(&frame[2 + reading->index * 2]);
	}

	return frame[1] / 2;
}

ZTEST(sensor_decoder_helpers, test_decode_tagged)
{
	const struct sensor_raw_frames tagged_frames = {
		.frames = tagged,
		.size = sizeof(tagged),
		.frame_len = tagged_len,
		.decode_frame = tagged_decode,
		.timestamp_ns = LAST_TS_NS,
		.period_ns = PERIOD_NS,
	};
	struct sensor_chan_spec chan = {SENSOR_CHAN_DIE_TEMP, 0};
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[3];
	} out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(sensor_raw_frames_count(&tagged_frames, chan, &frame_count));
	zassert_equal(frame_count, 4);

	/* One reading per call, including splitting the frame holding two readings */
	for (int i = 0; i < 4; i++) {
		zassert_equal(sensor_decode_frames(&tagged_frames, chan, &fit, 1, &out), 1);
		zassert_equal(out.data.readings[0].value, i + 1);
		zassert_equal(q31_ts(&out.data, 0), LAST_TS_NS - (3 - i) * PERIOD_NS);
	}
	zassert_equal(sensor_decode_frames(&tagged_frames, chan, &fit, 1, &out), 0);

	fit = 0;
	zassert_equal(sensor_decode_frames(&tagged_frames, chan, &fit, 4, &out), 4);
	for (int i = 0; i < 4; i++) {
		zassert_equal(q31_reading(&out.data, i)->value, i + 1);
		zassert_equal(q31_ts(&out.data, i), LAST_TS_NS - (3 - i) * PERIOD_NS);
	}
}

ZTEST(sensor_decoder_helpers, test_decode_channel_absent)
{
	static const uint8_t other[] = {'B', 1, 0xff, 'B', 2, 0x01, 0x02};
	const struct sensor_raw_frames absent = {
		.frames = other,
		.size = sizeof(other),
		.frame_len = tagged_len,
		.decode_frame = tagged_decode,
	};
	struct sensor_raw_frames empty = absent;
	struct sensor_chan_spec chan = {SENSOR_CHAN_DIE_TEMP, 0};
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Frames of another sensor only */
	zassert_ok(sensor_raw_frames_count(&absent, chan, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(sensor_decode_frames(&absent, chan, &fit, 1, &out), -ENODATA);
	zassert_equal(fit, 0);

	/* No frame at all */
	empty.size = 0;
	zassert_equal(sensor_decode_frames(&empty, chan, &fit, 1, &out), 0);

	/* An unsupported channel is rejected even without output requested */
	zassert_equal(sensor_decode_frames(&absent,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0},
					   &fit, 0, &out),
		      -ENOTSUP);
}

static int four_values_decode(const uint8_t *frame, struct sensor_chan_spec chan_spec,
			      const void *user_data, struct sensor_frame_reading *reading)
{
	ARG_UNUSED(user_data);

	if (reading != NULL) {
		for (int i = 0; i < 4; i++) {
			reading->values[i] = frame[i];
		}
		/* Timestamp carried by the frame */
		reading->timestamp_ns = 1000U * frame[4];
	}

	return 1;
}

ZTEST(sensor_decoder_helpers, test_decode_four_values)
{
	static const uint8_t raw[] = {1, 2, 3, 4, 10, 5, 6, 7, 8, 30};
	const struct sensor_raw_frames four = {
		.frames = raw,
		.size = sizeof(raw),
		.frame_size = 5,
		.decode_frame = four_values_decode,
		.shift = 3,
	};
	struct sensor_chan_spec chan = {SENSOR_CHAN_GAME_ROTATION_VECTOR, 0};
	struct {
		struct sensor_game_rotation_vector_data data;
		struct sensor_game_rotation_vector_sample_data extra[1];
	} out;
	uint32_t fit = 0;

	zassert_equal(sensor_decode_frames(&four, chan, &fit, 2, &out), 2);
	zassert_equal(out.data.shift, 3);
	zassert_equal(out.data.header.base_timestamp_ns, 10000);
	zassert_equal(out.data.readings[0].timestamp_delta, 0);
	zassert_equal(out.data.readings[0].w, 4);
	zassert_equal(out.extra[0].timestamp_delta, 20000);
	zassert_equal(out.extra[0].x, 5);
	zassert_equal(out.extra[0].w, 8);
}

ZTEST(sensor_decoder_helpers, test_decode_chan_idx_and_layout)
{
	struct sensor_raw_frames custom = frames;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* A private channel decoded as three values, on channel index 1 */
	custom.num_values = 3;
	custom.max_chan_idx = 1;

	zassert_equal(sensor_decode_frames(&custom,
					   (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					   &fit, 1, &out),
		      1);
	check_three_axis_reading(&out, 0, 0);
}

ZTEST(sensor_decoder_helpers, test_size_info)
{
	size_t base;
	size_t frame;

	zassert_ok(sensor_decode_frames_size_info(
		(struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 0}, 0, &base, &frame));
	zassert_equal(base, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(sensor_decode_frames_size_info(
		(struct sensor_chan_spec){SENSOR_CHAN_GBIAS_XYZ, 0}, 0, &base, &frame));
	zassert_equal(base, sizeof(struct sensor_three_axis_data));

	zassert_ok(sensor_decode_frames_size_info(
		(struct sensor_chan_spec){SENSOR_CHAN_GAME_ROTATION_VECTOR, 0}, 0, &base, &frame));
	zassert_equal(base, sizeof(struct sensor_game_rotation_vector_data));
	zassert_equal(frame, sizeof(struct sensor_game_rotation_vector_sample_data));

	zassert_ok(sensor_decode_frames_size_info(
		(struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 0}, 0, &base, &frame));
	zassert_equal(base, sizeof(struct sensor_q31_data));
	zassert_equal(frame, sizeof(struct sensor_q31_sample_data));

	zassert_ok(sensor_decode_frames_size_info((struct sensor_chan_spec){SENSOR_CHAN_ALL + 1, 0},
						  3, &base, &frame));
	zassert_equal(base, sizeof(struct sensor_three_axis_data));

	zassert_equal(sensor_decode_frames_size_info((struct sensor_chan_spec){SENSOR_CHAN_PROX, 0},
						     0, &base, &frame),
		      -ENOTSUP);
}
