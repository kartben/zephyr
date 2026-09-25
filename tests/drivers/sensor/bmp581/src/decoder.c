/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <string.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/sensor/bmp581.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bmp581.h"
#include "bmp581_decoder.h"
#include "bmp581_emul.h"

/* Bits of bmp581_encoded_header.channels */
#define ENC_CHAN_TEMP  BIT(0)
#define ENC_CHAN_PRESS BIT(1)

/* Allowed error of a decoded value, in micro-degC or micro-kPa */
#define TOLERANCE_MICRO 50

#define NUM_FRAMES 4
#define LAST_TS_NS 100000000000ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bmp581));

static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_AMBIENT_TEMP, 0};
static const struct sensor_chan_spec chan_press = {SENSOR_CHAN_PRESS, 0};

static uint8_t enc_buf[sizeof(struct bmp581_encoded_data) +
		       NUM_FRAMES * sizeof(struct bmp581_frame)] __aligned(8);
static struct bmp581_encoded_data *const edata = (struct bmp581_encoded_data *)enc_buf;

/* Raw samples of frame i: temperature in 1/65536 degC, pressure in 1/64 Pa */
static const int32_t raw_temp[NUM_FRAMES] = {1638400, -655360, 8388607, -8388608};
static const uint32_t raw_press[NUM_FRAMES] = {6400000, 1920000, 9437184, 16777215};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static int64_t temp_to_micro(int32_t raw)
{
	return (int64_t)raw * 1000000 / 65536;
}

static int64_t press_to_micro(uint32_t raw)
{
	return (int64_t)raw * 1000000 / 64000;
}

static void check_value(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);
}

static void check_reading(struct sensor_chan_spec chan, int8_t shift,
			  const struct sensor_q31_sample_data *reading, int frame)
{
	zassert_equal(shift, 15);

	if (chan.chan_type == SENSOR_CHAN_AMBIENT_TEMP) {
		check_value(reading->temperature, shift, temp_to_micro(raw_temp[frame]));
	} else {
		check_value(reading->pressure, shift, press_to_micro(raw_press[frame]));
	}
}

static uint8_t *frame_get(int frame)
{
	return &enc_buf[offsetof(struct bmp581_encoded_data, payload) +
			frame * sizeof(struct bmp581_frame)];
}

static void fill(uint8_t channels, uint8_t events, uint8_t odr, uint8_t fifo_count)
{
	memset(enc_buf, 0, sizeof(enc_buf));
	edata->header.channels = channels;
	edata->header.events = events;
	edata->header.timestamp = LAST_TS_NS;
	edata->header.press_en = 1;
	edata->header.fifo_count = fifo_count;
	edata->header.odr = odr;

	for (int i = 0; i < NUM_FRAMES; i++) {
		sys_put_le24((uint32_t)raw_temp[i], &frame_get(i)[0]);
		sys_put_le24(raw_press[i], &frame_get(i)[3]);
	}
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(enc_buf, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(enc_buf, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(enc_buf, chan, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(enc_buf, chan, &fit, 1, &out), -ENODATA);
}

ZTEST(bmp581_decoder, test_size_info)
{
	const struct sensor_chan_spec chans[] = {chan_temp, chan_press};
	size_t base_size;
	size_t frame_size;

	ARRAY_FOR_EACH(chans, i) {
		zassert_ok(decoder->get_size_info(chans[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(bmp581_decoder, test_one_shot)
{
	const struct sensor_chan_spec chans[] = {chan_temp, chan_press};
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit;

	fill(ENC_CHAN_TEMP | ENC_CHAN_PRESS, 0, BMP581_DT_ODR_50_HZ, 0);

	for (int f = 0; f < NUM_FRAMES; f++) {
		/* Put the samples of frame f in the single sample payload */
		memcpy(frame_get(0), frame_get(f), sizeof(struct bmp581_frame));

		ARRAY_FOR_EACH(chans, i) {
			zassert_ok(decoder->get_frame_count(enc_buf, chans[i], &frame_count));
			zassert_equal(frame_count, 1);

			fit = 0;
			zassert_equal(decoder->decode(enc_buf, chans[i], &fit, 4, &out), 1);
			zassert_equal(out.header.reading_count, 1);
			zassert_equal(out.header.base_timestamp_ns, LAST_TS_NS);
			zassert_equal(out.readings[0].timestamp_delta, 0);
			check_reading(chans[i], out.shift, &out.readings[0], f);
			zassert_equal(decoder->decode(enc_buf, chans[i], &fit, 4, &out), 0);
		}
	}

	fit = 0;
	zassert_equal(decoder->decode(enc_buf, chan_temp, &fit, 0, &out), 0);

	zassert_false(decoder->has_trigger(enc_buf, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(enc_buf, SENSOR_TRIG_FIFO_WATERMARK));

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_PRESS, 1});
}

ZTEST(bmp581_decoder, test_one_shot_missing_channel)
{
	struct sensor_q31_data out;
	uint32_t fit = 0;

	/* Only the temperature was requested */
	fill(ENC_CHAN_TEMP, 0, BMP581_DT_ODR_50_HZ, 0);
	zassert_equal(decoder->decode(enc_buf, chan_temp, &fit, 1, &out), 1);
	check_reading(chan_temp, out.shift, &out.readings[0], 0);
	check_no_data(chan_press);

	/* Only the pressure was requested */
	fill(ENC_CHAN_PRESS, 0, BMP581_DT_ODR_50_HZ, 0);
	fit = 0;
	zassert_equal(decoder->decode(enc_buf, chan_press, &fit, 1, &out), 1);
	check_reading(chan_press, out.shift, &out.readings[0], 0);
	check_no_data(chan_temp);

	/* Pressure disabled */
	fill(ENC_CHAN_TEMP | ENC_CHAN_PRESS, 0, BMP581_DT_ODR_50_HZ, 0);
	edata->header.press_en = 0;
	fit = 0;
	zassert_equal(decoder->decode(enc_buf, chan_temp, &fit, 1, &out), 1);
	check_no_data(chan_press);
}

ZTEST(bmp581_decoder, test_drdy)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* The FIFO watermark is recorded but ignored without the FIFO event */
	fill(ENC_CHAN_TEMP | ENC_CHAN_PRESS, BMP581_EVENT_DRDY, BMP581_DT_ODR_50_HZ, 3);

	zassert_ok(decoder->get_frame_count(enc_buf, chan_press, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(enc_buf, chan_press, &fit, 4, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, LAST_TS_NS);
	check_reading(chan_press, out.shift, &out.readings[0], 0);
	zassert_equal(decoder->decode(enc_buf, chan_press, &fit, 4, &out), 0);

	zassert_true(decoder->has_trigger(enc_buf, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(enc_buf, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(bmp581_decoder, test_fifo)
{
	const struct sensor_chan_spec chans[] = {chan_temp, chan_press};
	const uint64_t period_ns = 20000000ULL;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit;

	fill(ENC_CHAN_TEMP | ENC_CHAN_PRESS, BMP581_EVENT_FIFO_WM, BMP581_DT_ODR_50_HZ, NUM_FRAMES);

	ARRAY_FOR_EACH(chans, i) {
		zassert_ok(decoder->get_frame_count(enc_buf, chans[i], &frame_count));
		zassert_equal(frame_count, NUM_FRAMES);

		/* All readings in one call, with room for more */
		fit = 0;
		memset(&out, 0, sizeof(out));
		zassert_equal(decoder->decode(enc_buf, chans[i], &fit, NUM_FRAMES + 2, &out),
			      NUM_FRAMES);
		zassert_equal(out.data.header.reading_count, NUM_FRAMES);
		zassert_equal(out.data.header.base_timestamp_ns,
			      LAST_TS_NS - (NUM_FRAMES - 1) * period_ns);

		for (int f = 0; f < NUM_FRAMES; f++) {
			zassert_equal(readings[f].timestamp_delta, f * period_ns);
			check_reading(chans[i], out.data.shift, &readings[f], f);
		}

		zassert_equal(decoder->decode(enc_buf, chans[i], &fit, NUM_FRAMES, &out), 0);

		/* One reading per call */
		fit = 0;
		for (int f = 0; f < NUM_FRAMES; f++) {
			memset(&out, 0, sizeof(out));
			zassert_equal(decoder->decode(enc_buf, chans[i], &fit, 1, &out), 1);
			zassert_equal(out.data.header.reading_count, 1);
			zassert_equal(out.data.header.base_timestamp_ns +
					      readings[0].timestamp_delta,
				      LAST_TS_NS - (NUM_FRAMES - 1 - f) * period_ns);
			check_reading(chans[i], out.data.shift, &readings[0], f);
			/* Nothing is written past the requested reading */
			zassert_equal(out.extra[0].value, 0);
		}

		zassert_equal(decoder->decode(enc_buf, chans[i], &fit, 1, &out), 0);
	}

	zassert_true(decoder->has_trigger(enc_buf, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(enc_buf, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(enc_buf, SENSOR_TRIG_FIFO_FULL));

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});

	/* Two readings, then the next two */
	fit = 0;
	zassert_equal(decoder->decode(enc_buf, chan_temp, &fit, 2, &out), 2);
	zassert_equal(decoder->decode(enc_buf, chan_temp, &fit, 2, &out), 2);
	zassert_equal(out.data.header.base_timestamp_ns, LAST_TS_NS - period_ns);
	zassert_equal(readings[1].timestamp_delta, period_ns);
	check_reading(chan_temp, out.data.shift, &readings[0], 2);
	check_reading(chan_temp, out.data.shift, &readings[1], 3);
}

ZTEST(bmp581_decoder, test_fifo_odr)
{
	static const struct {
		uint8_t odr;
		uint64_t period_ns;
	} odrs[] = {
		{BMP581_DT_ODR_240_HZ, 4166666ULL},
		{BMP581_DT_ODR_218_5_HZ, 4576659ULL},
		{BMP581_DT_ODR_100_2_HZ, 9980039ULL},
		{BMP581_DT_ODR_0_5_HZ, 2000000000ULL},
		{BMP581_DT_ODR_0_250_HZ, 4000000000ULL},
		{BMP581_DT_ODR_0_125_HZ, 8000000000ULL},
	};
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;

	ARRAY_FOR_EACH(odrs, i) {
		uint32_t fit = 0;
		int f = 0;
		int rc;

		fill(ENC_CHAN_TEMP | ENC_CHAN_PRESS, BMP581_EVENT_FIFO_WM, odrs[i].odr, NUM_FRAMES);

		/* Batches end early when the deltas do not fit 32 bits */
		while ((rc = decoder->decode(enc_buf, chan_press, &fit, NUM_FRAMES, &out)) > 0) {
			for (int r = 0; r < rc; r++, f++) {
				zassert_equal(out.data.header.base_timestamp_ns +
						      readings[r].timestamp_delta,
					      LAST_TS_NS - (NUM_FRAMES - 1 - f) * odrs[i].period_ns,
					      "odr %u frame %d", odrs[i].odr, f);
				check_reading(chan_press, out.data.shift, &readings[r], f);
			}
		}

		zassert_equal(rc, 0);
		zassert_equal(f, NUM_FRAMES);
	}
}

#define READ_BUF_SIZE 64

SENSOR_DT_READ_IODEV(bmp581_iodev_all, DT_NODELABEL(bmp581), {SENSOR_CHAN_AMBIENT_TEMP, 0},
		     {SENSOR_CHAN_PRESS, 0});
SENSOR_DT_READ_IODEV(bmp581_iodev_temp, DT_NODELABEL(bmp581), {SENSOR_CHAN_AMBIENT_TEMP, 0});
/* A completion can be consumed before its SQE is back in the pool: one SQE per read */
RTIO_DEFINE(bmp581_rtio, 2, 2);

ZTEST(bmp581_decoder, test_read_one_shot)
{
	const struct emul *target = EMUL_DT_GET(DT_NODELABEL(bmp581));
	uint8_t buf[READ_BUF_SIZE] __aligned(8);
	struct sensor_q31_data out;
	uint64_t before_ns;
	uint32_t fit = 0;

	bmp581_emul_set_temp_raw(target, raw_temp[1]);
	bmp581_emul_set_press_raw(target, raw_press[0]);

	before_ns = k_ticks_to_ns_floor64(k_uptime_ticks());
	zassert_ok(sensor_read(&bmp581_iodev_all, &bmp581_rtio, buf, sizeof(buf)));

	zassert_equal(decoder->decode(buf, chan_temp, &fit, 1, &out), 1);
	zassert_true(out.header.base_timestamp_ns >= before_ns);
	check_value(out.readings[0].temperature, out.shift, temp_to_micro(raw_temp[1]));

	fit = 0;
	zassert_equal(decoder->decode(buf, chan_press, &fit, 1, &out), 1);
	check_value(out.readings[0].pressure, out.shift, press_to_micro(raw_press[0]));

	zassert_ok(sensor_read(&bmp581_iodev_temp, &bmp581_rtio, buf, sizeof(buf)));
	fit = 0;
	zassert_equal(decoder->decode(buf, chan_temp, &fit, 1, &out), 1);
	check_value(out.readings[0].temperature, out.shift, temp_to_micro(raw_temp[1]));
	fit = 0;
	zassert_equal(decoder->decode(buf, chan_press, &fit, 1, &out), -ENODATA);
}

ZTEST_SUITE(bmp581_decoder, NULL, NULL, NULL, NULL, NULL);
