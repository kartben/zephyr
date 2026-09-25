/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "iis3dwb.h"

/* Allowed error of a decoded acceleration, in micro-m/s^2 */
#define TOLERANCE_UMS2 500
/* Allowed error of a decoded temperature, in micro-degC */
#define TOLERANCE_UC   10

#define FIFO_TAG_SHIFT 3U

#define TS_NS 1000000000ULL

#define ACCEL_PERIOD_NS 37453ULL   /* 26.7 kHz */
#define TEMP_PERIOD_NS  9615384ULL /* 104 Hz */

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(iis3dwb));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_x = {SENSOR_CHAN_ACCEL_X, 0};
static const struct sensor_chan_spec chan_y = {SENSOR_CHAN_ACCEL_Y, 0};
static const struct sensor_chan_spec chan_z = {SENSOR_CHAN_ACCEL_Z, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

/* Sensitivity in ug/LSB and expected shift for each range */
static const struct {
	uint8_t range;
	int32_t ug_per_lsb;
	int8_t shift;
} ranges[] = {
	{IIS3DWB_DT_FS_2G, 61, 5},
	{IIS3DWB_DT_FS_4G, 122, 6},
	{IIS3DWB_DT_FS_8G, 244, 7},
	{IIS3DWB_DT_FS_16G, 488, 8},
};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_accel(q31_t value, int8_t shift, int16_t raw, int32_t ug_per_lsb)
{
	int64_t expected = (int64_t)raw * ug_per_lsb * SENSOR_G / 1000000;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_temp(q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = 25000000 + (int64_t)raw * 1000000 / 256;
	int64_t actual = q31_to_micro(value, shift);

	zassert_equal(shift, 9);
	zassert_within(actual, expected, TOLERANCE_UC, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
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
}

static void check_size_info(struct sensor_chan_spec chan, size_t base, size_t frame)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan, &base_size, &frame_size));
	zassert_equal(base_size, base);
	zassert_equal(frame_size, frame);
}

static struct iis3dwb_rtio_data sample;

static void fill_sample(uint8_t range, const int16_t *accel, const int16_t *temp)
{
	memset(&sample, 0, sizeof(sample));
	sample.header.timestamp = TS_NS;
	sample.header.range = range;

	if (accel != NULL) {
		sample.has_accel = 1;
		for (int i = 0; i < 3; i++) {
			sys_put_le16((uint16_t)accel[i], (uint8_t *)&sample.accel[i]);
		}
	}

	if (temp != NULL) {
		sample.has_temp = 1;
		sys_put_le16((uint16_t)*temp, (uint8_t *)&sample.temp);
	}
}

ZTEST(iis3dwb_decoder, test_size_info)
{
	check_size_info(chan_xyz, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));
	check_size_info(chan_x, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(chan_y, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(chan_z, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));

	if (IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		check_size_info(chan_temp, sizeof(struct sensor_q31_data),
				sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(iis3dwb_decoder, test_single_accel)
{
	static const int16_t raw[3] = {16384, -32768, 32767};
	const uint8_t *buffer = (const uint8_t *)&sample;

	for (size_t r = 0; r < ARRAY_SIZE(ranges); r++) {
		struct sensor_three_axis_data out;
		struct sensor_q31_data out_1;
		uint16_t frame_count;
		uint32_t fit = 0;

		fill_sample(ranges[r].range, raw, NULL);

		zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
		zassert_equal(frame_count, 1);

		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.header.base_timestamp_ns, TS_NS);
		zassert_equal(out.readings[0].timestamp_delta, 0);
		zassert_equal(out.shift, ranges[r].shift);
		check_accel(out.readings[0].x, out.shift, raw[0], ranges[r].ug_per_lsb);
		check_accel(out.readings[0].y, out.shift, raw[1], ranges[r].ug_per_lsb);
		check_accel(out.readings[0].z, out.shift, raw[2], ranges[r].ug_per_lsb);
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

		for (int axis = 0; axis < 3; axis++) {
			struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_X + axis, 0};

			zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
			zassert_equal(frame_count, 1);

			fit = 0;
			zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out_1), 1);
			zassert_equal(out_1.header.reading_count, 1);
			zassert_equal(out_1.header.base_timestamp_ns, TS_NS);
			zassert_equal(out_1.readings[0].timestamp_delta, 0);
			zassert_equal(out_1.shift, ranges[r].shift);
			check_accel(out_1.readings[0].value, out_1.shift, raw[axis],
				    ranges[r].ug_per_lsb);
			zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out_1), 0);
		}
	}

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	if (!IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		check_unsupported(buffer, chan_temp);
	}
}

ZTEST(iis3dwb_decoder, test_single_no_data)
{
	static const int16_t raw[3] = {1, 2, 3};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Temperature read alone, or a buffer completed without data */
	fill_sample(IIS3DWB_DT_FS_2G, NULL, NULL);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -ENODATA);
	zassert_ok(decoder->get_frame_count(buffer, chan_z, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_z, &fit, 1, &out), -ENODATA);

	if (IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		fill_sample(IIS3DWB_DT_FS_2G, raw, NULL);
		zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), -ENODATA);
	}
}

ZTEST(iis3dwb_decoder, test_single_temp)
{
	static const int16_t raw_accel[3] = {-61, 0, 61};
	static const int16_t raw_temps[] = {0, 2560, -1280, 32767, -32768};
	const uint8_t *buffer = (const uint8_t *)&sample;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_ENABLE_TEMP);

	for (size_t i = 0; i < ARRAY_SIZE(raw_temps); i++) {
		struct sensor_q31_data out;
		struct sensor_three_axis_data out_xyz;
		uint16_t frame_count;
		uint32_t fit = 0;

		fill_sample(IIS3DWB_DT_FS_16G, raw_accel, &raw_temps[i]);

		zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
		zassert_equal(frame_count, 1);

		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.header.base_timestamp_ns, TS_NS);
		zassert_equal(out.readings[0].timestamp_delta, 0);
		check_temp(out.readings[0].temperature, out.shift, raw_temps[i]);
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);

		/* Both sensors in one buffer */
		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out_xyz), 1);
		zassert_equal(out_xyz.shift, 8);
		check_accel(out_xyz.readings[0].x, out_xyz.shift, raw_accel[0], 488);
		check_accel(out_xyz.readings[0].z, out_xyz.shift, raw_accel[2], 488);
	}

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
}

ZTEST(iis3dwb_decoder, test_single_trigger)
{
	static const int16_t raw[3] = {1, 2, 3};
	const uint8_t *buffer = (const uint8_t *)&sample;

	/* Stream data ready buffer: int_status holds STATUS_REG */
	fill_sample(IIS3DWB_DT_FS_2G, raw, NULL);
	sample.header.int_status = 0xFF;

	zassert_equal(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY),
		      IS_ENABLED(CONFIG_IIS3DWB_STREAM));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	sample.header.int_status = 0;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

#define NUM_WORDS    8
#define NUM_XL_WORDS 4

static struct {
	struct iis3dwb_fifo_data hdr;
	uint8_t words[NUM_WORDS * IIS3DWB_FIFO_ITEM_LEN];
} __packed fifo;

static int16_t fifo_accel(int i, int axis)
{
	static const int16_t base[3] = {1000, -1000, 32767};

	return (axis == 2) ? base[axis] - i : base[axis] * (i + 1);
}

static const int16_t fifo_temps[] = {512, -256};

static void put_word(int idx, uint8_t tag, int16_t a, int16_t b, int16_t c)
{
	uint8_t *word = &fifo.words[idx * IIS3DWB_FIFO_ITEM_LEN];

	/* Tag counter and parity bits are set to check that they are ignored */
	word[0] = (uint8_t)(tag << FIFO_TAG_SHIFT) | 0x07U;
	sys_put_le16((uint16_t)a, &word[1]);
	sys_put_le16((uint16_t)b, &word[3]);
	sys_put_le16((uint16_t)c, &word[5]);
}

static void put_xl_word(int idx, int i)
{
	put_word(idx, IIS3DWB_XL_TAG, fifo_accel(i, 0), fifo_accel(i, 1), fifo_accel(i, 2));
}

static void fill_fifo(uint8_t int_status)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.range = IIS3DWB_DT_FS_4G;
	fifo.hdr.header.int_status = int_status;
	fifo.hdr.header.timestamp = TS_NS;
	fifo.hdr.accel_odr = IIS3DWB_DT_ODR_26k7Hz;
	fifo.hdr.accel_batch_odr = IIS3DWB_DT_XL_BATCHED_AT_26k7Hz;
	fifo.hdr.temp_batch_odr = IIS3DWB_DT_TEMP_BATCHED_AT_104Hz;
	fifo.hdr.ts_batch_odr = IIS3DWB_DT_DEC_TS_1;
	fifo.hdr.fifo_count = NUM_WORDS;

	/* Accelerometer, temperature and timestamp words interleaved, and an unknown tag */
	put_xl_word(0, 0);
	put_xl_word(1, 1);
	put_word(2, IIS3DWB_TEMPERATURE_TAG, fifo_temps[0], 0, 0);
	put_word(3, IIS3DWB_TIMESTAMP_TAG, 0x1234, 0x5678, 0);
	put_xl_word(4, 2);
	put_word(5, 0x1E, 0x7FFF, 0x7FFF, 0x7FFF);
	put_word(6, IIS3DWB_TEMPERATURE_TAG, fifo_temps[1], 0, 0);
	put_xl_word(7, 3);
}

static uint64_t accel_ts(int i)
{
	return TS_NS - (NUM_XL_WORDS - 1 - i) * ACCEL_PERIOD_NS;
}

ZTEST(iis3dwb_decoder, test_fifo_accel)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_XL_WORDS - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;
	int i;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);

	fill_fifo(0x80);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, NUM_XL_WORDS);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_XL_WORDS, &out), NUM_XL_WORDS);
	zassert_equal(out.data.header.reading_count, NUM_XL_WORDS);
	zassert_equal(out.data.header.base_timestamp_ns, accel_ts(0));
	zassert_equal(out.data.shift, 6);

	for (i = 0; i < NUM_XL_WORDS; i++) {
		zassert_equal(readings[i].timestamp_delta, i * ACCEL_PERIOD_NS);
		check_accel(readings[i].x, out.data.shift, fifo_accel(i, 0), 122);
		check_accel(readings[i].y, out.data.shift, fifo_accel(i, 1), 122);
		check_accel(readings[i].z, out.data.shift, fifo_accel(i, 2), 122);
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_XL_WORDS, &out), 0);

	/* In two calls, resuming after the temperature and timestamp words */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 3, &out), 3);
	for (i = 0; i < 3; i++) {
		zassert_equal(out.data.header.base_timestamp_ns + readings[i].timestamp_delta,
			      accel_ts(i));
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 3, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.header.base_timestamp_ns + readings[0].timestamp_delta, accel_ts(3));
	check_accel(readings[0].x, out.data.shift, fifo_accel(3, 0), 122);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 3, &out), 0);
}

ZTEST(iis3dwb_decoder, test_fifo_accel_axis)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);

	fill_fifo(0x80);

	for (int axis = 0; axis < 3; axis++) {
		struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_X + axis, 0};
		struct sensor_q31_data out;
		uint16_t frame_count;
		uint32_t fit = 0;

		zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
		zassert_equal(frame_count, NUM_XL_WORDS);

		/* One reading per call */
		for (int i = 0; i < NUM_XL_WORDS; i++) {
			zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
			zassert_equal(out.header.reading_count, 1);
			zassert_equal(out.header.base_timestamp_ns +
					      out.readings[0].timestamp_delta,
				      accel_ts(i));
			zassert_equal(out.shift, 6);
			check_accel(out.readings[0].value, out.shift, fifo_accel(i, axis), 122);
		}
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
	}

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	if (!IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		check_unsupported(buffer, chan_temp);
	}
}

ZTEST(iis3dwb_decoder, test_fifo_temp)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);
	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_ENABLE_TEMP);

	fill_fifo(0x80);

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, ARRAY_SIZE(fifo_temps));

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 2, &out), 2);
	zassert_equal(out.data.header.reading_count, 2);
	zassert_equal(out.data.header.base_timestamp_ns, TS_NS - TEMP_PERIOD_NS);
	zassert_equal(readings[0].timestamp_delta, 0);
	zassert_equal(readings[1].timestamp_delta, TEMP_PERIOD_NS);
	check_temp(readings[0].temperature, out.data.shift, fifo_temps[0]);
	check_temp(readings[1].temperature, out.data.shift, fifo_temps[1]);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 2, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < 2; i++) {
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
		zassert_equal(out.data.header.base_timestamp_ns + readings[0].timestamp_delta,
			      TS_NS - (1 - i) * TEMP_PERIOD_NS);
		check_temp(readings[0].temperature, out.data.shift, fifo_temps[i]);
	}
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);
}

ZTEST(iis3dwb_decoder, test_fifo_not_batched)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_XL_WORDS - 1];
	} out;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[1];
	} out_temp;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	const struct sensor_q31_sample_data *temp_readings = out_temp.data.readings;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);

	/* Without a batch rate, all readings get the header timestamp */
	fill_fifo(0x80);
	fifo.hdr.header.range = IIS3DWB_DT_FS_16G;
	fifo.hdr.accel_batch_odr = IIS3DWB_DT_XL_NOT_BATCHED;
	fifo.hdr.temp_batch_odr = IIS3DWB_DT_TEMP_NOT_BATCHED;

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, NUM_XL_WORDS, &out), NUM_XL_WORDS);
	zassert_equal(out.data.header.base_timestamp_ns, TS_NS);
	zassert_equal(out.data.shift, 8);

	for (int i = 0; i < NUM_XL_WORDS; i++) {
		zassert_equal(readings[i].timestamp_delta, 0);
		check_accel(readings[i].x, out.data.shift, fifo_accel(i, 0), 488);
		check_accel(readings[i].y, out.data.shift, fifo_accel(i, 1), 488);
		check_accel(readings[i].z, out.data.shift, fifo_accel(i, 2), 488);
	}

	if (IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 2, &out_temp), 2);
		zassert_equal(out_temp.data.header.base_timestamp_ns, TS_NS);
		zassert_equal(temp_readings[0].timestamp_delta, 0);
		zassert_equal(temp_readings[1].timestamp_delta, 0);
	}
}

ZTEST(iis3dwb_decoder, test_fifo_no_temp)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);
	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_ENABLE_TEMP);

	/* FIFO data without temperature words */
	fill_fifo(0x80);
	put_xl_word(2, 2);
	put_xl_word(6, 3);
	fifo.hdr.fifo_count = 7;

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 0, &out), -ENODATA);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 5);
}

ZTEST(iis3dwb_decoder, test_fifo_trigger)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);

	/* int_status holds FIFO_STATUS2: FIFO_WTM_IA is bit 7, FIFO_FULL_IA bit 5 */
	fill_fifo(0x80);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	/* Bit 0 is DIFF_FIFO bit 8, not a data ready flag */
	fill_fifo(0x21);
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

ZTEST(iis3dwb_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_IIS3DWB_STREAM);

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.int_status = 0x80;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	if (IS_ENABLED(CONFIG_IIS3DWB_ENABLE_TEMP)) {
		zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);
	}
}

ZTEST_SUITE(iis3dwb_decoder, NULL, NULL, NULL, NULL, NULL);
