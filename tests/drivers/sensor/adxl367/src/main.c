/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "adxl367.h"

/* Allowed error of a decoded acceleration, in micro-m/s^2 */
#define TOLERANCE_UMS2 1000
/* Allowed error of a decoded temperature, in micro-degrees Celsius */
#define TOLERANCE_UC   100

#define SAMPLE_TS_NS 123456789ULL
#define ACCEL_SHIFT  5
#define TEMP_SHIFT   8

#define CHAN(type) ((struct sensor_chan_spec){(type), 0})

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl367));

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Check an acceleration against a 14-bit raw sample */
static void check_accel(q31_t value, int8_t shift, int32_t raw, enum adxl367_range range)
{
	int64_t expected = (int64_t)raw * SENSOR_G / (ADXL367_ACCEL_2G_LSB_PER_G >> range);
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_UMS2, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

/* Check a temperature against a 14-bit raw sample */
static void check_temp(q31_t value, int8_t shift, int32_t raw)
{
	int64_t expected =
		(int64_t)(raw - ADXL367_TEMP_25C) * 1000000 / ADXL367_TEMP_SENSITIVITY + 25000000;
	int64_t actual = q31_to_micro(value, shift);

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

ZTEST(adxl367_decoder, test_size_info)
{
	static const enum sensor_channel q31_chans[] = {
		SENSOR_CHAN_ACCEL_X,
		SENSOR_CHAN_ACCEL_Y,
		SENSOR_CHAN_ACCEL_Z,
		SENSOR_CHAN_DIE_TEMP,
	};
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(CHAN(SENSOR_CHAN_ACCEL_XYZ), &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (size_t i = 0; i < ARRAY_SIZE(q31_chans); i++) {
		zassert_ok(decoder->get_size_info(CHAN(q31_chans[i]), &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_equal(decoder->get_size_info(CHAN(SENSOR_CHAN_GYRO_XYZ), &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

static void check_single(enum adxl367_range range, int8_t shift)
{
	static const enum sensor_channel axis_chans[] = {
		SENSOR_CHAN_ACCEL_X,
		SENSOR_CHAN_ACCEL_Y,
		SENSOR_CHAN_ACCEL_Z,
	};
	static const int16_t raw[] = {4001, -2000, -8191};
	struct adxl367_sample_data sample = {
		.xyz = {.x = raw[0], .y = raw[1], .z = raw[2], .range = range},
		.raw_temp = ADXL367_TEMP_25C + 10 * ADXL367_TEMP_SENSITIVITY,
		.timestamp = SAMPLE_TS_NS,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, shift);
	check_accel(out.readings[0].x, out.shift, raw[0], range);
	check_accel(out.readings[0].y, out.shift, raw[1], range);
	check_accel(out.readings[0].z, out.shift, raw[2], range);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 0);

	for (size_t i = 0; i < ARRAY_SIZE(axis_chans); i++) {
		zassert_ok(decoder->get_frame_count(buffer, CHAN(axis_chans[i]), &frame_count));
		zassert_equal(frame_count, 1);

		fit = 0;
		zassert_equal(decoder->decode(buffer, CHAN(axis_chans[i]), &fit, 1, &out_1), 1);
		zassert_equal(out_1.header.base_timestamp_ns, SAMPLE_TS_NS);
		zassert_equal(out_1.shift, shift);
		check_accel(out_1.readings[0].value, out_1.shift, raw[i], range);
	}

	zassert_ok(decoder->get_frame_count(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &frame_count));
	zassert_equal(frame_count, 1);

	fit = 0;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out_1), 1);
	zassert_equal(out_1.header.base_timestamp_ns, SAMPLE_TS_NS);
	zassert_equal(out_1.shift, TEMP_SHIFT);
	check_temp(out_1.readings[0].temperature, out_1.shift, sample.raw_temp);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out_1), 0);

	sample.raw_temp = -1000;
	fit = 0;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out_1), 1);
	check_temp(out_1.readings[0].temperature, out_1.shift, sample.raw_temp);

	/* The first bytes of a single sample must not be taken for a FIFO header */
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, CHAN(SENSOR_CHAN_GYRO_XYZ));
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
}

ZTEST(adxl367_decoder, test_single)
{
	check_single(ADXL367_2G_RANGE, 5);
	check_single(ADXL367_4G_RANGE, 6);
	check_single(ADXL367_8G_RANGE, 7);
}

ZTEST(adxl367_decoder, test_adxl366)
{
	const struct sensor_decoder_api *decoder_366 = SENSOR_DECODER_DT_GET(DT_NODELABEL(adxl366));
	struct adxl367_sample_data sample = {
		.xyz = {.x = 1000, .y = -1000, .z = 2000, .range = ADXL367_2G_RANGE},
		.raw_temp = ADXL367_TEMP_25C,
		.timestamp = SAMPLE_TS_NS,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	zassert_equal(decoder_366->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS_NS);
	zassert_equal(out.shift, ACCEL_SHIFT);
	check_accel(out.readings[0].x, out.shift, sample.xyz.x, ADXL367_2G_RANGE);
	check_accel(out.readings[0].y, out.shift, sample.xyz.y, ADXL367_2G_RANGE);
	check_accel(out.readings[0].z, out.shift, sample.xyz.z, ADXL367_2G_RANGE);
}

ZTEST(adxl367_decoder, test_single_bad_range)
{
	struct adxl367_sample_data sample = {
		.xyz = {.range = 3},
		.raw_temp = ADXL367_TEMP_25C,
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_q31_data out;
	uint32_t fit = 0;

	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_X), &fit, 1, &out), -EINVAL);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out), 1);
	check_temp(out.readings[0].temperature, out.shift, sample.raw_temp);
}

#ifdef CONFIG_ADXL367_STREAM

#define LAST_TS_NS  10000000000ULL
#define MAX_PACKETS 60
#define MAX_SAMPLES 4

/* Samples of the FIFO packets, as bits of the format */
#define FMT_X   BIT(0)
#define FMT_Y   BIT(1)
#define FMT_Z   BIT(2)
#define FMT_T   BIT(3)
#define FMT_A   BIT(4)
#define FMT_XYZ (FMT_X | FMT_Y | FMT_Z)

static struct {
	struct adxl367_fifo_data hdr;
	uint8_t data[MAX_PACKETS * MAX_SAMPLES * 2];
} __packed fifo;

static union {
	struct sensor_three_axis_data xyz;
	struct sensor_q31_data q31;
	uint8_t raw[sizeof(struct sensor_three_axis_data) +
		    MAX_PACKETS * sizeof(struct sensor_three_axis_sample_data)];
} out;

static const uint64_t period_ns[] = {
	[ADXL367_ODR_12P5HZ] = 80000000ULL, [ADXL367_ODR_25HZ] = 40000000ULL,
	[ADXL367_ODR_50HZ] = 20000000ULL,   [ADXL367_ODR_100HZ] = 10000000ULL,
	[ADXL367_ODR_200HZ] = 5000000ULL,   [ADXL367_ODR_400HZ] = 2500000ULL,
};

/*
 * 14-bit value of sample n of the format (0 to 2: axes, 3: temperature, 4: ADC) in a packet.
 * It is a multiple of 64 so that all read modes represent it exactly.
 */
static int32_t sample_raw(uint8_t packet, uint8_t n)
{
	return 64 * (((int32_t)packet * 23 + (int32_t)n * 7) % 128 - 60);
}

static void put_sample(enum adxl367_fifo_read_mode mode, size_t pos, uint8_t n, int32_t raw)
{
	/* Temperature and ADC samples share channel ID 3 */
	uint16_t chid = MIN(n, 3U) << 14;
	uint16_t raw12 = (uint16_t)(raw / 4) & 0x0FFFU;

	switch (mode) {
	case ADXL367_8B:
		fifo.data[pos] = (uint8_t)(raw / 64);
		break;
	case ADXL367_12B:
		/* 12-bit samples packed MSB first */
		for (size_t bit = 0; bit < 12; bit++) {
			size_t out_bit = pos * 12 + bit;

			if ((raw12 & BIT(11 - bit)) != 0U) {
				fifo.data[out_bit / 8] |= BIT(7 - (out_bit % 8));
			}
		}
		break;
	case ADXL367_12B_CHID:
		sys_put_le16(chid | raw12, &fifo.data[pos * 2]);
		break;
	case ADXL367_14B_CHID:
		sys_put_be16(chid | ((uint16_t)raw & 0x3FFFU), &fifo.data[pos * 2]);
		break;
	}
}

static void fill_fifo(enum adxl367_fifo_read_mode mode, uint8_t format, uint8_t packets,
		      enum adxl367_odr odr)
{
	uint8_t samples = 0;
	size_t pos = 0;

	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.has_x = (format & FMT_X) != 0U;
	fifo.hdr.has_y = (format & FMT_Y) != 0U;
	fifo.hdr.has_z = (format & FMT_Z) != 0U;
	fifo.hdr.has_tmp = (format & FMT_T) != 0U;
	fifo.hdr.has_adc = (format & FMT_A) != 0U;
	fifo.hdr.fifo_read_mode = mode;
	fifo.hdr.accel_odr = odr;
	fifo.hdr.range = ADXL367_2G_RANGE;
	fifo.hdr.int_status = ADXL367_STATUS_FIFO_WATERMARK;
	fifo.hdr.timestamp = LAST_TS_NS;

	for (uint8_t p = 0; p < packets; p++) {
		for (uint8_t n = 0; n < 5; n++) {
			if ((format & BIT(n)) != 0U) {
				put_sample(mode, pos++, n, sample_raw(p, n));
			}
		}
	}

	samples = pos / packets;

	switch (mode) {
	case ADXL367_8B:
		fifo.hdr.packet_size = samples;
		fifo.hdr.fifo_byte_count = pos;
		break;
	case ADXL367_12B:
		/* Number of samples of a packet */
		fifo.hdr.packet_size = samples;
		fifo.hdr.fifo_byte_count = pos * 12 / 8;
		break;
	default:
		fifo.hdr.packet_size = samples * 2;
		fifo.hdr.fifo_byte_count = pos * 2;
		break;
	}
}

static bool chan_present(enum sensor_channel type, uint8_t format)
{
	switch (type) {
	case SENSOR_CHAN_ACCEL_X:
		return (format & FMT_X) != 0U;
	case SENSOR_CHAN_ACCEL_Y:
		return (format & FMT_Y) != 0U;
	case SENSOR_CHAN_ACCEL_Z:
		return (format & FMT_Z) != 0U;
	case SENSOR_CHAN_ACCEL_XYZ:
		return (format & FMT_XYZ) != 0U;
	default:
		return (format & FMT_T) != 0U;
	}
}

/* Get values and timestamp of reading i of the output */
static const q31_t *reading_get(enum sensor_channel type, int i, uint64_t *ts)
{
	if (type == SENSOR_CHAN_ACCEL_XYZ) {
		const struct sensor_three_axis_sample_data *readings = out.xyz.readings;

		*ts = out.xyz.header.base_timestamp_ns + readings[i].timestamp_delta;
		return readings[i].values;
	}

	const struct sensor_q31_sample_data *readings = out.q31.readings;

	*ts = out.q31.header.base_timestamp_ns + readings[i].timestamp_delta;
	return &readings[i].value;
}

static void check_reading(enum sensor_channel type, const q31_t *values, int8_t shift,
			  uint8_t format, uint8_t packet)
{
	switch (type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		for (uint8_t axis = 0; axis < 3; axis++) {
			if ((format & BIT(axis)) != 0U) {
				check_accel(values[axis], shift, sample_raw(packet, axis),
					    ADXL367_2G_RANGE);
			} else {
				zassert_equal(values[axis], 0);
			}
		}
		break;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		check_accel(values[0], shift, sample_raw(packet, type - SENSOR_CHAN_ACCEL_X),
			    ADXL367_2G_RANGE);
		break;
	default:
		check_temp(values[0], shift, sample_raw(packet, 3));
		break;
	}
}

static void check_fifo(enum adxl367_fifo_read_mode mode, uint8_t format, uint8_t packets,
		       enum adxl367_odr odr)
{
	static const enum sensor_channel chans[] = {
		SENSOR_CHAN_ACCEL_X,   SENSOR_CHAN_ACCEL_Y,  SENSOR_CHAN_ACCEL_Z,
		SENSOR_CHAN_ACCEL_XYZ, SENSOR_CHAN_DIE_TEMP,
	};
	const uint8_t *buffer = (const uint8_t *)&fifo;
	/* Readings of a batch, whose timestamp deltas fit in 32 bits */
	const uint32_t batch = UINT32_MAX / period_ns[odr] + 1U;

	fill_fifo(mode, format, packets, odr);

	for (size_t c = 0; c < ARRAY_SIZE(chans); c++) {
		enum sensor_channel type = chans[c];
		struct sensor_chan_spec chan = CHAN(type);
		int8_t shift = (type == SENSOR_CHAN_DIE_TEMP) ? TEMP_SHIFT : ACCEL_SHIFT;
		uint16_t frame_count;
		uint32_t fit = 0;
		uint64_t ts;
		const q31_t *values;

		TC_PRINT("mode %d format 0x%02x packets %d channel %d\n", mode, format, packets,
			 type);

		if (!chan_present(type, format)) {
			/* The buffer holds data, but no reading of this channel */
			zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
			zassert_equal(frame_count, 0);
			zassert_equal(decoder->decode(buffer, chan, &fit, packets, &out), -ENODATA);
			continue;
		}

		zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
		zassert_equal(frame_count, packets);

		/* All readings, in as few calls as the timestamp deltas allow */
		for (uint8_t first = 0, n; first < packets; first += n) {
			n = MIN((uint32_t)(packets - first), batch);
			zassert_equal(decoder->decode(buffer, chan, &fit, packets, &out), n);
			zassert_equal(out.q31.header.reading_count, n);
			zassert_equal(out.q31.shift, shift);

			for (uint8_t i = 0; i < n; i++) {
				values = reading_get(type, i, &ts);
				zassert_equal(
					ts, LAST_TS_NS - (packets - 1 - first - i) * period_ns[odr],
					"reading %d", first + i);
				check_reading(type, values, shift, format, first + i);
			}
		}

		zassert_equal(decoder->decode(buffer, chan, &fit, packets, &out), 0);

		/* One reading per call */
		fit = 0;
		for (uint8_t i = 0; i < packets; i++) {
			zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
			zassert_equal(out.q31.header.reading_count, 1);
			zassert_equal(out.q31.shift, shift);
			values = reading_get(type, 0, &ts);
			zassert_equal(ts, LAST_TS_NS - (packets - 1 - i) * period_ns[odr],
				      "reading %d", i);
			check_reading(type, values, shift, format, i);
		}

		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
	}

	check_unsupported(buffer, CHAN(SENSOR_CHAN_GYRO_XYZ));
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_DIE_TEMP, 1});
}

ZTEST(adxl367_decoder, test_fifo_14b_chid)
{
	check_fifo(ADXL367_14B_CHID, FMT_XYZ, 4, ADXL367_ODR_100HZ);
	check_fifo(ADXL367_14B_CHID, FMT_XYZ | FMT_T, 3, ADXL367_ODR_400HZ);
	check_fifo(ADXL367_14B_CHID, FMT_XYZ | FMT_A, 2, ADXL367_ODR_200HZ);
	check_fifo(ADXL367_14B_CHID, FMT_X, 5, ADXL367_ODR_50HZ);
	check_fifo(ADXL367_14B_CHID, FMT_Y | FMT_T, 2, ADXL367_ODR_25HZ);
	check_fifo(ADXL367_14B_CHID, FMT_Z | FMT_A, 3, ADXL367_ODR_12P5HZ);
}

ZTEST(adxl367_decoder, test_fifo_12b_chid)
{
	check_fifo(ADXL367_12B_CHID, FMT_XYZ, 4, ADXL367_ODR_100HZ);
	check_fifo(ADXL367_12B_CHID, FMT_X | FMT_T, 3, ADXL367_ODR_12P5HZ);
}

ZTEST(adxl367_decoder, test_fifo_8b)
{
	check_fifo(ADXL367_8B, FMT_XYZ, 4, ADXL367_ODR_100HZ);
	check_fifo(ADXL367_8B, FMT_XYZ | FMT_T, 3, ADXL367_ODR_200HZ);
	check_fifo(ADXL367_8B, FMT_X | FMT_T, 3, ADXL367_ODR_50HZ);
	check_fifo(ADXL367_8B, FMT_Z, 6, ADXL367_ODR_400HZ);
}

ZTEST(adxl367_decoder, test_fifo_12b)
{
	/* Packets of an odd number of samples alternate between two bit alignments */
	check_fifo(ADXL367_12B, FMT_XYZ, 4, ADXL367_ODR_100HZ);
	check_fifo(ADXL367_12B, FMT_X, 6, ADXL367_ODR_25HZ);
	check_fifo(ADXL367_12B, FMT_Z, 2, ADXL367_ODR_12P5HZ);
	check_fifo(ADXL367_12B, FMT_XYZ | FMT_T, 3, ADXL367_ODR_400HZ);
	check_fifo(ADXL367_12B, FMT_XYZ | FMT_A, 3, ADXL367_ODR_200HZ);
	check_fifo(ADXL367_12B, FMT_X | FMT_T, 3, ADXL367_ODR_50HZ);
	check_fifo(ADXL367_12B, FMT_Y | FMT_A, 5, ADXL367_ODR_100HZ);
}

ZTEST(adxl367_decoder, test_fifo_slow_odr)
{
	/* At 12.5 Hz, the timestamp deltas of 55 readings or more do not fit in 32 bits */
	check_fifo(ADXL367_14B_CHID, FMT_XYZ | FMT_T, 60, ADXL367_ODR_12P5HZ);
	check_fifo(ADXL367_12B, FMT_XYZ, 60, ADXL367_ODR_12P5HZ);
	check_fifo(ADXL367_8B, FMT_Y, 55, ADXL367_ODR_12P5HZ);
}

ZTEST(adxl367_decoder, test_fifo_range)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint32_t fit = 0;

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ | FMT_T, 1, ADXL367_ODR_100HZ);

	fifo.hdr.range = ADXL367_4G_RANGE;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 1);
	zassert_equal(out.xyz.shift, 6);
	check_accel(out.xyz.readings[0].x, out.xyz.shift, sample_raw(0, 0), ADXL367_4G_RANGE);

	fifo.hdr.range = ADXL367_8G_RANGE;
	fit = 0;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 1);
	zassert_equal(out.xyz.shift, 7);
	check_accel(out.xyz.readings[0].z, out.xyz.shift, sample_raw(0, 2), ADXL367_8G_RANGE);

	/* The temperature does not depend on the range */
	fit = 0;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out), 1);
	zassert_equal(out.q31.shift, TEMP_SHIFT);
	check_temp(out.q31.readings[0].temperature, out.q31.shift, sample_raw(0, 3));
}

ZTEST(adxl367_decoder, test_fifo_chid_mismatch)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint32_t fit = 0;

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ, 2, ADXL367_ODR_100HZ);

	/* Give the Y sample of the second packet the channel ID of X */
	fifo.data[8] &= 0x3FU;

	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 2, &out), 2);
	check_accel(out.xyz.readings[0].y, out.xyz.shift, sample_raw(0, 1), ADXL367_2G_RANGE);
	check_accel(out.xyz.readings[1].x, out.xyz.shift, sample_raw(1, 0), ADXL367_2G_RANGE);
	zassert_equal(out.xyz.readings[1].y, 0);
	check_accel(out.xyz.readings[1].z, out.xyz.shift, sample_raw(1, 2), ADXL367_2G_RANGE);
}

ZTEST(adxl367_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.is_fifo = 1;
	fifo.hdr.int_status = ADXL367_STATUS_FIFO_OVERRUN;
	fifo.hdr.timestamp = LAST_TS_NS;

	zassert_ok(decoder->get_frame_count(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &frame_count));
	zassert_equal(frame_count, 0);
	zassert_ok(decoder->get_frame_count(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 0);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, CHAN(SENSOR_CHAN_GYRO_XYZ));
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(adxl367_decoder, test_fifo_invalid)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ, 2, ADXL367_ODR_100HZ);
	fifo.hdr.accel_odr = ADXL367_ODR_400HZ + 1;
	zassert_equal(decoder->get_frame_count(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &frame_count),
		      -EINVAL);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), -EINVAL);

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ, 2, ADXL367_ODR_100HZ);
	fifo.hdr.packet_size = 4;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), -EINVAL);

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ, 2, ADXL367_ODR_100HZ);
	fifo.hdr.fifo_read_mode = ADXL367_14B_CHID + 1;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), -EINVAL);

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ | FMT_T, 2, ADXL367_ODR_100HZ);
	fifo.hdr.range = 3;
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), -EINVAL);
	zassert_equal(decoder->decode(buffer, CHAN(SENSOR_CHAN_DIE_TEMP), &fit, 1, &out), 1);
}

ZTEST(adxl367_decoder, test_fifo_has_trigger)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	fill_fifo(ADXL367_14B_CHID, FMT_XYZ, 1, ADXL367_ODR_100HZ);

	fifo.hdr.int_status = ADXL367_STATUS_DATA_RDY;
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	fifo.hdr.int_status = ADXL367_STATUS_FIFO_WATERMARK;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	fifo.hdr.int_status = ADXL367_STATUS_FIFO_OVERRUN;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	fifo.hdr.int_status = 0xFFU;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_TAP));
}

#endif /* CONFIG_ADXL367_STREAM */

ZTEST_SUITE(adxl367_decoder, NULL, NULL, NULL, NULL, NULL);
