/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "bma4xx_decoder.h"
#include "bma4xx_defs.h"

/* Allowed error of a decoded value, in micro-units (micro-m/s^2 or micro-degrees C) */
#define TOLERANCE_MICRO 1000

#define SAMPLE_TS_NS 123456789ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(bma4xx));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_axis[] = {
	{SENSOR_CHAN_ACCEL_X, 0},
	{SENSOR_CHAN_ACCEL_Y, 0},
	{SENSOR_CHAN_ACCEL_Z, 0},
};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* 12-bit sample to micro-m/s^2: full scale (2048 LSB) is 2 << accel_fs g */
static int64_t raw_to_micro(int16_t raw, uint8_t accel_fs)
{
	return (int64_t)raw * (2 << accel_fs) * SENSOR_G / 2048;
}

static void check_accel(q31_t value, int8_t shift, int16_t raw, uint8_t accel_fs)
{
	int64_t expected = raw_to_micro(raw, accel_fs);
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

/* Store a 12-bit sample left-justified in a 16-bit little endian word, as the sensor does */
static void put_accel(uint8_t *data, int16_t x, int16_t y, int16_t z)
{
	sys_put_le16((uint16_t)x << 4, &data[0]);
	sys_put_le16((uint16_t)y << 4, &data[2]);
	sys_put_le16((uint16_t)z << 4, &data[4]);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

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

ZTEST(bma4xx_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	check_size_info(chan_xyz, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		check_size_info(chan_axis[i], sizeof(struct sensor_q31_data),
				sizeof(struct sensor_q31_sample_data));
	}

	if (IS_ENABLED(CONFIG_BMA4XX_TEMPERATURE)) {
		check_size_info(chan_temp, sizeof(struct sensor_q31_data),
				sizeof(struct sensor_q31_sample_data));
	} else {
		zassert_equal(decoder->get_size_info(chan_temp, &base_size, &frame_size), -ENOTSUP);
	}

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

static void check_single(uint8_t accel_fs, int16_t x, int16_t y, int16_t z)
{
	const int16_t raw[] = {x, y, z};
	struct bma4xx_encoded_data edata = {
		.header.timestamp = SAMPLE_TS_NS,
		.header.accel_fs = accel_fs,
	};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	put_accel(edata.accel_xyz_raw_data, x, y, z);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	out.readings[0].timestamp_delta = 0xdeadbeef;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 5 + accel_fs);
	check_accel(out.readings[0].x, out.shift, x, accel_fs);
	check_accel(out.readings[0].y, out.shift, y, accel_fs);
	check_accel(out.readings[0].z, out.shift, z, accel_fs);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
		zassert_equal(frame_count, 1);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 1);
		zassert_equal(out_1.header.reading_count, 1);
		zassert_equal(out_1.header.base_timestamp_ns, SAMPLE_TS_NS);
		zassert_equal(out_1.shift, 5 + accel_fs);
		check_accel(out_1.readings[0].value, out_1.shift, raw[i], accel_fs);
		zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out_1), 0);
	}

	/* Nothing requested */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 0, &out), 0);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	if (!IS_ENABLED(CONFIG_BMA4XX_TEMPERATURE)) {
		check_unsupported(buffer, chan_temp);
	}
}

ZTEST(bma4xx_decoder, test_single_2g)
{
	check_single(BMA4XX_RANGE_2G, 1024, -1024, 2047);
}

ZTEST(bma4xx_decoder, test_single_4g)
{
	check_single(BMA4XX_RANGE_4G, -2048, 512, -1);
}

ZTEST(bma4xx_decoder, test_single_8g)
{
	check_single(BMA4XX_RANGE_8G, 256, 0, -256);
}

ZTEST(bma4xx_decoder, test_single_16g)
{
	check_single(BMA4XX_RANGE_16G, 2047, -2048, 128);
}

ZTEST(bma4xx_decoder, test_single_fifo_flag)
{
	struct bma4xx_encoded_data edata = {
		.header.timestamp = SAMPLE_TS_NS,
		.header.accel_fs = BMA4XX_RANGE_2G,
	};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFDEF(CONFIG_BMA4XX_STREAM);

	/* The encoder only writes the FIFO flag with CONFIG_BMA4XX_STREAM */
	edata.header.is_fifo = 1;
	put_accel(edata.accel_xyz_raw_data, 1024, 0, -1024);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	check_accel(out.readings[0].x, out.shift, 1024, BMA4XX_RANGE_2G);
	check_accel(out.readings[0].z, out.shift, -1024, BMA4XX_RANGE_2G);
}

#ifdef CONFIG_BMA4XX_TEMPERATURE

static void check_temp(int8_t raw)
{
	struct bma4xx_encoded_data edata = {
		.header.timestamp = SAMPLE_TS_NS,
		.header.accel_fs = BMA4XX_RANGE_2G,
		.temp = raw,
	};
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;
	int64_t expected = ((int64_t)raw + 23) * 1000000;

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, BMA4XX_TEMP_SHIFT);
	zassert_within(q31_to_micro(out.readings[0].temperature, out.shift), expected,
		       TOLERANCE_MICRO);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 0);
}

ZTEST(bma4xx_decoder, test_single_temp)
{
	check_temp(0);
	check_temp(-10);
	check_temp(INT8_MAX);
	check_temp(INT8_MIN);
}

#endif /* CONFIG_BMA4XX_TEMPERATURE */

#ifdef CONFIG_BMA4XX_STREAM

#define LAST_TS_NS   10000000000ULL
#define FIFO_SIZE    128
#define MAX_READINGS 8

#define HEADER_ACCEL      0x84
#define HEADER_AUX        0x90
#define HEADER_AUX_ACCEL  0x94
#define HEADER_SKIP       0x40
#define HEADER_SENSORTIME 0x44
#define HEADER_CONFIG     0x48
#define HEADER_OVER_READ  0x80
#define HEADER_CTRL_OVER  0x60
#define HEADER_INVALID    0x00

static struct {
	struct bma4xx_fifo_data hdr;
	uint8_t data[FIFO_SIZE];
} __packed fifo;

static size_t fifo_len;

/*
 * Output of several readings, accessed through a pointer: indexing readings[] beyond the
 * first reading of a declared struct object is undefined behavior.
 */
#define OUT_BUF_SIZE                                                                               \
	(sizeof(struct sensor_three_axis_data) +                                                   \
	 (MAX_READINGS - 1) * sizeof(struct sensor_three_axis_sample_data))

static __aligned(8) uint8_t out_buf[OUT_BUF_SIZE];

static void fifo_init(uint8_t accel_fs, uint8_t accel_odr, uint8_t int_status)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.accel_fs = accel_fs;
	fifo.hdr.header.timestamp = LAST_TS_NS;
	fifo.hdr.int_status = int_status;
	fifo.hdr.accel_odr = accel_odr;
	fifo_len = 0;
}

static void fifo_put(uint8_t header, size_t len)
{
	zassert_true(fifo_len + 1 + len <= FIFO_SIZE);
	fifo.data[fifo_len] = header;
	/* Filler for payloads that are not accelerometer data */
	memset(&fifo.data[fifo_len + 1], 0xa5, len);
	fifo_len += 1 + len;
	fifo.hdr.fifo_count = fifo_len;
}

static void fifo_put_accel(uint8_t header, int16_t x, int16_t y, int16_t z)
{
	size_t aux_len = (header == HEADER_AUX_ACCEL) ? BMA4XX_FIFO_M_LENGTH : 0;

	fifo_put(header, aux_len + BMA4XX_FIFO_A_LENGTH);
	put_accel(&fifo.data[fifo_len - BMA4XX_FIFO_A_LENGTH], x, y, z);
}

#define NUM_ACCEL 3

static const int16_t fifo_raw[NUM_ACCEL][3] = {
	{100, -200, 1024},
	{-2048, 2047, 0},
	{-5, 5, -1024},
};

/*
 * Accelerometer frames mixed with control and auxiliary frames, which do not hold readings
 * and do not take a time slot.
 */
static void fifo_fill_mixed(uint8_t accel_fs, uint8_t accel_odr)
{
	fifo_init(accel_fs, accel_odr, BMA4XX_BIT_INT_STAT_1_FWM_INT);
	fifo_put(HEADER_CONFIG, BMA4XX_FIFO_CF_LENGTH);
	fifo_put_accel(HEADER_ACCEL, fifo_raw[0][0], fifo_raw[0][1], fifo_raw[0][2]);
	fifo_put(HEADER_SKIP, BMA4XX_FIFO_CF_LENGTH);
	fifo_put(HEADER_AUX, BMA4XX_FIFO_M_LENGTH);
	fifo_put_accel(HEADER_AUX_ACCEL, fifo_raw[1][0], fifo_raw[1][1], fifo_raw[1][2]);
	fifo_put(HEADER_SENSORTIME, BMA4XX_FIFO_ST_LENGTH);
	fifo_put_accel(HEADER_ACCEL, fifo_raw[2][0], fifo_raw[2][1], fifo_raw[2][2]);
}

static uint64_t reading_ts(uint64_t period_ns, int idx)
{
	return LAST_TS_NS - (NUM_ACCEL - 1 - idx) * period_ns;
}

static void check_fifo(uint8_t accel_fs, uint8_t accel_odr, uint64_t period_ns)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data *out = (struct sensor_three_axis_data *)out_buf;
	const struct sensor_three_axis_sample_data *readings = out->readings;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)out_buf;
	struct sensor_q31_data out_1;
	uint16_t frame_count;
	uint32_t fit = 0;

	fifo_fill_mixed(accel_fs, accel_odr);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, NUM_ACCEL);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_READINGS, out), NUM_ACCEL);
	zassert_equal(out->header.reading_count, NUM_ACCEL);
	zassert_equal(out->header.base_timestamp_ns, reading_ts(period_ns, 0));
	zassert_equal(out->shift, 5 + accel_fs);

	for (int i = 0; i < NUM_ACCEL; i++) {
		zassert_equal(out->header.base_timestamp_ns + readings[i].timestamp_delta,
			      reading_ts(period_ns, i));
		check_accel(readings[i].x, out->shift, fifo_raw[i][0], accel_fs);
		check_accel(readings[i].y, out->shift, fifo_raw[i][1], accel_fs);
		check_accel(readings[i].z, out->shift, fifo_raw[i][2], accel_fs);
	}

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_READINGS, out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < NUM_ACCEL; i++) {
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, out), 1);
		zassert_equal(out->header.reading_count, 1);
		zassert_equal(out->header.base_timestamp_ns + readings[0].timestamp_delta,
			      reading_ts(period_ns, i));
		check_accel(readings[0].x, out->shift, fifo_raw[i][0], accel_fs);
		check_accel(readings[0].y, out->shift, fifo_raw[i][1], accel_fs);
		check_accel(readings[0].z, out->shift, fifo_raw[i][2], accel_fs);
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, out), 0);

	/* Single axes */
	for (int axis = 0; axis < ARRAY_SIZE(chan_axis); axis++) {
		zassert_ok(decoder->get_frame_count(buffer, chan_axis[axis], &frame_count));
		zassert_equal(frame_count, NUM_ACCEL);

		fit = 0;
		for (int i = 0; i < NUM_ACCEL; i++) {
			zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_1), 1);
			zassert_equal(out_1.shift, 5 + accel_fs);
			zassert_equal(out_1.header.base_timestamp_ns +
					      out_1.readings[0].timestamp_delta,
				      reading_ts(period_ns, i));
			check_accel(out_1.readings[0].value, out_1.shift, fifo_raw[i][axis],
				    accel_fs);
		}
		zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, 1, &out_1), 0);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_axis[axis], &fit, MAX_READINGS, out_q31),
			      NUM_ACCEL);
		zassert_equal(out_q31->header.reading_count, NUM_ACCEL);
		for (int i = 0; i < NUM_ACCEL; i++) {
			zassert_equal(out_q31->header.base_timestamp_ns +
					      out_q31->readings[i].timestamp_delta,
				      reading_ts(period_ns, i));
			check_accel(out_q31->readings[i].value, out_q31->shift, fifo_raw[i][axis],
				    accel_fs);
		}
	}

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	/* The temperature is not stored in the FIFO */
	check_unsupported(buffer, chan_temp);
}

ZTEST(bma4xx_decoder, test_fifo_100hz)
{
	check_fifo(BMA4XX_RANGE_2G, BMA4XX_ODR_100, 10000000ULL);
}

ZTEST(bma4xx_decoder, test_fifo_0_78125hz)
{
	check_fifo(BMA4XX_RANGE_4G, BMA4XX_ODR_0_78125, 1280000000ULL);
}

ZTEST(bma4xx_decoder, test_fifo_12_5hz)
{
	check_fifo(BMA4XX_RANGE_8G, BMA4XX_ODR_12_5, 80000000ULL);
}

ZTEST(bma4xx_decoder, test_fifo_12800hz)
{
	check_fifo(BMA4XX_RANGE_16G, BMA4XX_ODR_12800, 78125ULL);
}

/* Readings at 0.78125 Hz spanning 8.96 s, more than UINT32_MAX ns */
#define NUM_LONG 8

ZTEST(bma4xx_decoder, test_fifo_long_span)
{
	const uint64_t period_ns = 1280000000ULL;
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data *out = (struct sensor_three_axis_data *)out_buf;
	const struct sensor_three_axis_sample_data *readings = out->readings;
	uint16_t frame_count;
	uint32_t fit = 0;
	int decoded = 0;
	int rc;

	fifo_init(BMA4XX_RANGE_4G, BMA4XX_ODR_0_78125, 0);
	for (int i = 0; i < NUM_LONG; i++) {
		if (i == 2) {
			fifo_put(HEADER_AUX, BMA4XX_FIFO_M_LENGTH);
		} else if (i == 5) {
			fifo_put(HEADER_SENSORTIME, BMA4XX_FIFO_ST_LENGTH);
		}
		fifo_put_accel(HEADER_ACCEL, 100 * i, -i, 2047 - i);
	}

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, NUM_LONG);

	/*
	 * A batch ends before a reading more than UINT32_MAX ns newer than its first one: four
	 * readings (3.84 s) per call.
	 */
	while ((rc = decoder->decode(buffer, chan_xyz, &fit, MAX_READINGS, out)) > 0) {
		zassert_equal(rc, NUM_LONG / 2);
		zassert_equal(out->header.reading_count, rc);
		zassert_equal(out->shift, 5 + BMA4XX_RANGE_4G);

		for (int i = 0; i < rc; i++, decoded++) {
			zassert_equal(out->header.base_timestamp_ns + readings[i].timestamp_delta,
				      LAST_TS_NS - (NUM_LONG - 1 - decoded) * period_ns);
			check_accel(readings[i].x, out->shift, 100 * decoded, BMA4XX_RANGE_4G);
			check_accel(readings[i].y, out->shift, -decoded, BMA4XX_RANGE_4G);
			check_accel(readings[i].z, out->shift, 2047 - decoded, BMA4XX_RANGE_4G);
		}
	}
	zassert_equal(rc, 0);
	zassert_equal(decoded, NUM_LONG);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < NUM_LONG; i++) {
		zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, out), 1);
		zassert_equal(out->header.base_timestamp_ns + readings[0].timestamp_delta,
			      LAST_TS_NS - (NUM_LONG - 1 - i) * period_ns);
		check_accel(readings[0].x, out->shift, 100 * i, BMA4XX_RANGE_4G);
	}
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, out), 0);
}

/* Check that the FIFO buffer holds one reading, the first frame 1, 2, 3 */
static void check_fifo_one_reading(void)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data *out = (struct sensor_three_axis_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_READINGS, out), 1);
	zassert_equal(out->header.reading_count, 1);
	zassert_equal(out->header.base_timestamp_ns + out->readings[0].timestamp_delta, LAST_TS_NS);
	check_accel(out->readings[0].x, out->shift, 1, BMA4XX_RANGE_2G);
	check_accel(out->readings[0].y, out->shift, 2, BMA4XX_RANGE_2G);
	check_accel(out->readings[0].z, out->shift, 3, BMA4XX_RANGE_2G);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, MAX_READINGS, out), 0);
}

ZTEST(bma4xx_decoder, test_fifo_end)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	/* The data stops at an over-read marker */
	fifo_init(BMA4XX_RANGE_2G, BMA4XX_ODR_100, BMA4XX_BIT_INT_STAT_1_FFULL_INT);
	fifo_put_accel(HEADER_ACCEL, 1, 2, 3);
	fifo_put(HEADER_OVER_READ, 1);
	fifo_put_accel(HEADER_ACCEL, 4, 5, 6);

	check_fifo_one_reading();
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	/* The data stops at a control over-read frame */
	fifo_init(BMA4XX_RANGE_2G, BMA4XX_ODR_100, 0);
	fifo_put_accel(HEADER_ACCEL, 1, 2, 3);
	fifo_put(HEADER_CTRL_OVER, 1);
	fifo_put_accel(HEADER_ACCEL, 4, 5, 6);

	check_fifo_one_reading();

	/* The data stops at a header with neither the regular nor the control bit */
	fifo_init(BMA4XX_RANGE_2G, BMA4XX_ODR_100, 0);
	fifo_put_accel(HEADER_ACCEL, 1, 2, 3);
	fifo_put(HEADER_INVALID, 0);
	fifo_put_accel(HEADER_ACCEL, 4, 5, 6);

	check_fifo_one_reading();

	/* A frame cut at the end of the data is ignored */
	fifo_init(BMA4XX_RANGE_2G, BMA4XX_ODR_100, 0);
	fifo_put_accel(HEADER_ACCEL, 1, 2, 3);
	fifo_put_accel(HEADER_ACCEL, 4, 5, 6);
	fifo.hdr.fifo_count = fifo_len - 1;

	check_fifo_one_reading();
}

ZTEST(bma4xx_decoder, test_fifo_no_accel)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Frames without accelerometer data */
	fifo_init(BMA4XX_RANGE_2G, BMA4XX_ODR_100, 0);
	fifo_put(HEADER_CONFIG, BMA4XX_FIFO_CF_LENGTH);
	fifo_put(HEADER_AUX, BMA4XX_FIFO_M_LENGTH);
	fifo_put(HEADER_SENSORTIME, BMA4XX_FIFO_ST_LENGTH);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_axis[0], &fit, 1, &out), -ENODATA);
}

ZTEST(bma4xx_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	fifo_init(0, 0, BMA4XX_BIT_INT_STAT_1_FWM_INT);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

#endif /* CONFIG_BMA4XX_STREAM */

ZTEST_SUITE(bma4xx_decoder, NULL, NULL, NULL, NULL, NULL);
