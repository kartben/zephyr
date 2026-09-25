/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/sensor/lis2dux12.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "lis2dux12.h"
#include "lis2dux12_decoder.h"

/* Sensitivity at +/-2 g in micro-g per LSB of a 16-bit sample, doubling with the range */
#define UG_PER_LSB_2G 61

/* Temperature sensitivity in LSB per degree Celsius, times 10 */
#define TEMP_LSB_PER_10C 3555

/* Allowed error of a decoded temperature, in micro-degrees Celsius */
#define TOLERANCE_UC 100

#define LAST_TS_NS   1000000000000ULL
#define MAX_READINGS 128

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(lis2dux12));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const struct sensor_chan_spec accel_chans[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
	{SENSOR_CHAN_ACCEL_XYZ, 0},
};

static const int8_t range_shift[] = {5, 6, 7, 8};

/* Raw values of one reading: accelerometer axes left-justified to 16 bits, or temperature */
struct raw_reading {
	int16_t v[3];
};

static struct {
	struct sensor_three_axis_data data;
	struct sensor_three_axis_sample_data extra[MAX_READINGS - 1];
} out3;

static struct {
	struct sensor_q31_data data;
	struct sensor_q31_sample_data extra[MAX_READINGS - 1];
} out1;

static struct {
	struct lis2dux12_fifo_data hdr;
	uint8_t words[MAX_READINGS][LIS2DUX12_FIFO_ITEM_LEN];
} __packed fifo;

static const uint8_t *const fifo_buf = (const uint8_t *)&fifo;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_accel(q31_t value, int8_t shift, int16_t raw, uint8_t range)
{
	int64_t expected = (int64_t)raw * UG_PER_LSB_2G * (1 << range) * SENSOR_G / 1000000;
	int64_t actual = q31_to_micro(value, shift);
	/* 0.01 % of the value, in micro-m/s^2 */
	int64_t tolerance = ((expected < 0) ? -expected : expected) / 10000 + 10;

	zassert_within(actual, expected, tolerance, "raw %d: got %lld, expected %lld", raw, actual,
		       expected);
}

static void check_temp(q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = 25000000 + (int64_t)raw * 10000000 / TEMP_LSB_PER_10C;
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UC, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out3), -ENOTSUP);
}

/*
 * Decode all accelerometer readings of a buffer with batches of max_count readings, and check
 * their values and absolute timestamps.
 */
static void check_accel_readings(const uint8_t *buffer, struct sensor_chan_spec chan,
				 uint16_t max_count, const struct raw_reading *raw, int num,
				 uint8_t range, uint64_t last_ts, uint64_t period)
{
	uint16_t frame_count;
	uint32_t fit = 0;
	int decoded = 0;
	int rc;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, num);

	while (decoded < num) {
		rc = decoder->decode(buffer, chan, &fit, max_count, &out3);
		zassert_true(rc > 0 && rc <= max_count, "decode returned %d", rc);
		zassert_equal(out3.data.header.reading_count, rc);
		zassert_equal(out3.data.shift, range_shift[range]);

		for (int i = 0; i < rc; i++) {
			const struct sensor_three_axis_sample_data *r = &out3.data.readings[i];
			int n = decoded + i;

			zassert_equal(out3.data.header.base_timestamp_ns + r->timestamp_delta,
				      last_ts - (uint64_t)(num - 1 - n) * period, "reading %d", n);
			for (int axis = 0; axis < 3; axis++) {
				check_accel(r->values[axis], out3.data.shift, raw[n].v[axis],
					    range);
			}
		}

		decoded += rc;
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, max_count, &out3), 0);
}

static void check_temp_readings(const uint8_t *buffer, uint16_t max_count,
				const struct raw_reading *raw, int num, uint64_t last_ts,
				uint64_t period)
{
	uint16_t frame_count;
	uint32_t fit = 0;
	int decoded = 0;
	int rc;

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, num);

	while (decoded < num) {
		rc = decoder->decode(buffer, chan_temp, &fit, max_count, &out1);
		zassert_true(rc > 0 && rc <= max_count, "decode returned %d", rc);
		zassert_equal(out1.data.header.reading_count, rc);
		zassert_equal(out1.data.shift, 9);

		for (int i = 0; i < rc; i++) {
			const struct sensor_q31_sample_data *r = &out1.data.readings[i];
			int n = decoded + i;

			zassert_equal(out1.data.header.base_timestamp_ns + r->timestamp_delta,
				      last_ts - (uint64_t)(num - 1 - n) * period, "reading %d", n);
			check_temp(r->temperature, out1.data.shift, raw[n].v[0]);
		}

		decoded += rc;
	}

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, max_count, &out1), 0);
}

static void put_sample(struct lis2dux12_rtio_data *sample, const struct raw_reading *acc)
{
	uint8_t *data = (uint8_t *)sample->acc;

	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)acc->v[i], &data[i * 2]);
	}
}

ZTEST(lis2dux12_decoder, test_size_info)
{
	const uint8_t buffer[sizeof(struct lis2dux12_rtio_data)] = {0};
	size_t base_size;
	size_t frame_size;

	ARRAY_FOR_EACH(accel_chans, i) {
		zassert_ok(decoder->get_size_info(accel_chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
		zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
	}

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	} else {
		check_unsupported(buffer, chan_temp);
	}

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(lis2dux12_decoder, test_sample_accel)
{
	static const struct raw_reading raw = {{16384, -32768, 32767}};
	struct lis2dux12_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	uint16_t frame_count;
	uint32_t fit;

	for (uint8_t range = LIS2DUX12_DT_FS_2G; range <= LIS2DUX12_DT_FS_16G; range++) {
		memset(&sample, 0, sizeof(sample));
		sample.header.range = range;
		sample.header.timestamp = LAST_TS_NS;
		sample.has_accel = 1;
		put_sample(&sample, &raw);

		ARRAY_FOR_EACH(accel_chans, i) {
			check_accel_readings(buffer, accel_chans[i], 1, &raw, 1, range, LAST_TS_NS,
					     0);
		}
	}

	zassert_equal(out3.data.readings[0].timestamp_delta, 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 0, &out3), 0);

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out1), -ENODATA);
	}

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(lis2dux12_decoder, test_sample_temp)
{
	static const struct raw_reading raw_acc = {{-8197, 0, 8197}};
	static const struct raw_reading raw_temp[] = {{{3555}}, {{-8888}}, {{32767}}};
	struct lis2dux12_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_ENABLE_TEMP);

	/* Read with SENSOR_CHAN_ALL */
	ARRAY_FOR_EACH(raw_temp, i) {
		memset(&sample, 0, sizeof(sample));
		sample.header.range = LIS2DUX12_DT_FS_8G;
		sample.header.timestamp = 123456789ULL;
		sample.has_accel = 1;
		sample.has_temp = 1;
		put_sample(&sample, &raw_acc);
		sys_put_le16((uint16_t)raw_temp[i].v[0], (uint8_t *)&sample.temp);

		check_temp_readings(buffer, 1, &raw_temp[i], 1, 123456789ULL, 0);
		check_accel_readings(buffer, chan_xyz, 1, &raw_acc, 1, LIS2DUX12_DT_FS_8G,
				     123456789ULL, 0);
	}
}

ZTEST(lis2dux12_decoder, test_sample_no_data)
{
	struct lis2dux12_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&sample, 0, sizeof(sample));
	sample.header.timestamp = LAST_TS_NS;

	ARRAY_FOR_EACH(accel_chans, i) {
		zassert_ok(decoder->get_frame_count(buffer, accel_chans[i], &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(buffer, accel_chans[i], &fit, 1, &out3), -ENODATA);
	}
}

ZTEST(lis2dux12_decoder, test_drdy)
{
	static const struct raw_reading raw = {{100, -100, 4096}};
	struct lis2dux12_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;

	/* Data ready stream buffer: STATUS register and OUTX_L to OUTZ_H */
	memset(&sample, 0, sizeof(sample));
	sample.header.range = LIS2DUX12_DT_FS_16G;
	sample.header.timestamp = LAST_TS_NS;
	sample.header.int_status = 0x01;
	sample.has_accel = 1;
	put_sample(&sample, &raw);

	check_accel_readings(buffer, chan_xyz, 1, &raw, 1, LIS2DUX12_DT_FS_16G, LAST_TS_NS, 0);

	zassert_equal(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY),
		      IS_ENABLED(CONFIG_LIS2DUX12_STREAM));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

static void fifo_init(uint8_t fifo_mode_sel, uint8_t odr, uint8_t bdr, uint8_t range,
		      uint16_t num_words, uint8_t int_status)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.range = range;
	fifo.hdr.header.timestamp = LAST_TS_NS;
	fifo.hdr.header.int_status = int_status;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.accel_batch_odr = bdr;
	fifo.hdr.fifo_mode_sel = fifo_mode_sel;
	fifo.hdr.fifo_count = num_words;
}

static void put_tag(uint8_t *word, uint8_t tag)
{
	/* The low bits hold the tag counter and parity */
	word[0] = (uint8_t)(tag << 3) | 0x5U;
}

/*
 * 12-bit accelerometer and 12-bit temperature, returned left-justified to 16 bits. Byte layout
 * of the data: X[7:0], Y[3:0] X[11:8], Y[11:4], Z[7:0], T[3:0] Z[11:8], T[11:4].
 */
static void put_xl12_temp(uint8_t *word, const int16_t xl[3], int16_t t, struct raw_reading *acc,
			  struct raw_reading *temp)
{
	const uint16_t x = (uint16_t)xl[0];
	const uint16_t y = (uint16_t)xl[1];
	const uint16_t z = (uint16_t)xl[2];
	const uint16_t tu = (uint16_t)t;

	put_tag(word, LIS2DUXXX_XL_TEMP_TAG);
	word[1] = (uint8_t)x;
	word[2] = (uint8_t)(((x >> 8) & 0x0FU) | ((y & 0x0FU) << 4));
	word[3] = (uint8_t)(y >> 4);
	word[4] = (uint8_t)z;
	word[5] = (uint8_t)(((z >> 8) & 0x0FU) | ((tu & 0x0FU) << 4));
	word[6] = (uint8_t)(tu >> 4);

	for (int i = 0; i < 3; i++) {
		acc->v[i] = (int16_t)(xl[i] * 16);
	}
	temp->v[0] = (int16_t)(t * 16);
}

static void put_xl16(uint8_t *word, const struct raw_reading *acc)
{
	put_tag(word, LIS2DUXXX_XL_TEMP_TAG);
	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)acc->v[i], &word[1 + i * 2]);
	}
}

/* Two 8-bit accelerometer samples, returned left-justified to 16 bits */
static void put_xl8_2x(uint8_t *word, const int8_t xl[6], struct raw_reading acc[2])
{
	put_tag(word, LIS2DUXXX_XL_ONLY_2X_TAG);
	for (int i = 0; i < 6; i++) {
		word[1 + i] = (uint8_t)xl[i];
		acc[i / 3].v[i % 3] = (int16_t)(xl[i] * 256);
	}
}

ZTEST(lis2dux12_decoder, test_fifo_xl12_temp)
{
	static const int16_t xl[3][3] = {{100, -200, 2047}, {-2048, 0, 1}, {5, 6, -7}};
	static const int16_t t[3] = {222, -100, 0};
	struct raw_reading acc[3];
	struct raw_reading temp[3];
	/* 100 Hz batched at ODR / 2 */
	const uint64_t period = 20000000ULL;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	fifo_init(0, LIS2DUX12_DT_ODR_100Hz, LIS2DUX12_DT_BDR_XL_ODR_DIV_2, LIS2DUX12_DT_FS_2G, 5,
		  0x80);
	put_xl12_temp(fifo.words[0], xl[0], t[0], &acc[0], &temp[0]);
	put_tag(fifo.words[1], LIS2DUXXX_TIMESTAMP_TAG);
	put_xl12_temp(fifo.words[2], xl[1], t[1], &acc[1], &temp[1]);
	put_tag(fifo.words[3], LIS2DUXXX_STEP_COUNTER_TAG);
	put_xl12_temp(fifo.words[4], xl[2], t[2], &acc[2], &temp[2]);

	ARRAY_FOR_EACH(accel_chans, i) {
		check_accel_readings(fifo_buf, accel_chans[i], 1, acc, 3, LIS2DUX12_DT_FS_2G,
				     LAST_TS_NS, period);
	}
	check_accel_readings(fifo_buf, chan_xyz, 3, acc, 3, LIS2DUX12_DT_FS_2G, LAST_TS_NS, period);

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		check_temp_readings(fifo_buf, 1, temp, 3, LAST_TS_NS, period);
		check_temp_readings(fifo_buf, 3, temp, 3, LAST_TS_NS, period);
	} else {
		check_unsupported(fifo_buf, chan_temp);
	}

	zassert_true(decoder->has_trigger(fifo_buf, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(fifo_buf, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(fifo_buf, SENSOR_TRIG_DATA_READY));

	check_unsupported(fifo_buf, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(fifo_buf, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(lis2dux12_decoder, test_fifo_xl16)
{
	static const struct raw_reading acc[2] = {{{32767, -32768, 1000}}, {{-1, 2, -3}}};
	/* 800 Hz batched at ODR */
	const uint64_t period = 1250000ULL;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	fifo_init(1, LIS2DUX12_DT_ODR_800Hz, LIS2DUX12_DT_BDR_XL_ODR, LIS2DUX12_DT_FS_16G, 3, 0x40);
	put_xl16(fifo.words[0], &acc[0]);
	put_tag(fifo.words[1], LIS2DUXXX_TIMESTAMP_TAG);
	put_xl16(fifo.words[2], &acc[1]);

	check_accel_readings(fifo_buf, chan_xyz, 1, acc, 2, LIS2DUX12_DT_FS_16G, LAST_TS_NS,
			     period);
	check_accel_readings(fifo_buf, chan_xyz, 2, acc, 2, LIS2DUX12_DT_FS_16G, LAST_TS_NS,
			     period);

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		/* No temperature in this FIFO mode */
		zassert_ok(decoder->get_frame_count(fifo_buf, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(fifo_buf, chan_temp, &fit, 2, &out1), -ENODATA);
	}

	zassert_false(decoder->has_trigger(fifo_buf, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(fifo_buf, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(lis2dux12_decoder, test_fifo_xl8_2x)
{
	static const int8_t xl[2][6] = {
		{1, -1, 127, -128, 64, -64},
		{10, 20, 30, -10, -20, -30},
	};
	struct raw_reading acc[5];
	/* 50 Hz batched at ODR */
	const uint64_t period = 20000000ULL;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	fifo_init(2, LIS2DUX12_DT_ODR_50Hz, LIS2DUX12_DT_BDR_XL_ODR, LIS2DUX12_DT_FS_8G, 4, 0x80);
	put_xl8_2x(fifo.words[0], xl[0], &acc[0]);
	put_tag(fifo.words[1], LIS2DUXXX_TIMESTAMP_TAG);
	/* XL_TEMP words hold a 16-bit accelerometer sample without temperature in this mode */
	acc[2] = (struct raw_reading){{-12345, 23456, -32768}};
	put_xl16(fifo.words[2], &acc[2]);
	put_xl8_2x(fifo.words[3], xl[1], &acc[3]);

	/* Batches of 1, 2 and 4 readings split the two samples of a word */
	check_accel_readings(fifo_buf, chan_xyz, 1, acc, 5, LIS2DUX12_DT_FS_8G, LAST_TS_NS, period);
	check_accel_readings(fifo_buf, chan_xyz, 2, acc, 5, LIS2DUX12_DT_FS_8G, LAST_TS_NS, period);
	check_accel_readings(fifo_buf, chan_xyz, 4, acc, 5, LIS2DUX12_DT_FS_8G, LAST_TS_NS, period);
	check_accel_readings(fifo_buf, chan_xyz, 5, acc, 5, LIS2DUX12_DT_FS_8G, LAST_TS_NS, period);

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		zassert_ok(decoder->get_frame_count(fifo_buf, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(fifo_buf, chan_temp, &fit, 2, &out1), -ENODATA);
	}
}

ZTEST(lis2dux12_decoder, test_fifo_long_period)
{
	static const struct raw_reading acc[3] = {{{1, 2, 3}}, {{4, 5, 6}}, {{7, 8, 9}}};
	/* 1 Hz batched at ODR / 64 */
	const uint64_t period = 64000000000ULL;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	fifo_init(1, LIS2DUX12_DT_ODR_1Hz_ULP, LIS2DUX12_DT_BDR_XL_ODR_DIV_64, LIS2DUX12_DT_FS_2G,
		  3, 0x80);
	for (int i = 0; i < 3; i++) {
		put_xl16(fifo.words[i], &acc[i]);
	}

	check_accel_readings(fifo_buf, chan_xyz, 1, acc, 3, LIS2DUX12_DT_FS_2G, LAST_TS_NS, period);
	check_accel_readings(fifo_buf, chan_xyz, 3, acc, 3, LIS2DUX12_DT_FS_2G, LAST_TS_NS, period);
}

ZTEST(lis2dux12_decoder, test_fifo_full)
{
	static struct raw_reading acc[MAX_READINGS];
	/* 12.5 Hz batched at ODR / 4 */
	const uint64_t period = 320000000ULL;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	/* The FIFO holds 128 words */
	fifo_init(1, LIS2DUX12_DT_ODR_12Hz5, LIS2DUX12_DT_BDR_XL_ODR_DIV_4, LIS2DUX12_DT_FS_4G,
		  MAX_READINGS, 0x40);
	for (int i = 0; i < MAX_READINGS; i++) {
		acc[i] = (struct raw_reading){{i * 100, -i * 100, i}};
		put_xl16(fifo.words[i], &acc[i]);
	}

	/*
	 * The readings span 40.64 s: each decode call returns the readings within UINT32_MAX ns of
	 * its first one, 14 at this period.
	 */
	check_accel_readings(fifo_buf, chan_xyz, MAX_READINGS, acc, MAX_READINGS,
			     LIS2DUX12_DT_FS_4G, LAST_TS_NS, period);
}

ZTEST(lis2dux12_decoder, test_fifo_empty)
{
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	fifo_init(0, 0, 0, 0, 0, 0x80);

	zassert_ok(decoder->get_frame_count(fifo_buf, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(fifo_buf, chan_xyz, &fit, 1, &out3), 0);

	if (IS_ENABLED(CONFIG_LIS2DUX12_ENABLE_TEMP)) {
		zassert_ok(decoder->get_frame_count(fifo_buf, chan_temp, &frame_count));
		zassert_equal(frame_count, 0);
		zassert_equal(decoder->decode(fifo_buf, chan_temp, &fit, 1, &out1), 0);
	}

	zassert_true(decoder->has_trigger(fifo_buf, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(lis2dux12_decoder, test_fifo_invalid_odr)
{
	static const struct raw_reading acc = {{1, 2, 3}};
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_LIS2DUX12_STREAM);

	fifo_init(1, LIS2DUX12_DT_ODR_END, LIS2DUX12_DT_BDR_XL_ODR, LIS2DUX12_DT_FS_2G, 1, 0x80);
	put_xl16(fifo.words[0], &acc);

	zassert_equal(decoder->get_frame_count(fifo_buf, chan_xyz, &frame_count), -EINVAL);
	zassert_equal(decoder->decode(fifo_buf, chan_xyz, &fit, 1, &out3), -EINVAL);
}

ZTEST_SUITE(lis2dux12_decoder, NULL, NULL, NULL, NULL, NULL);
