/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#define DT_DRV_COMPAT bosch_bmi08x_accel
#include "bmi08x.h"

/* Allowed error of a decoded value, in micro-m/s^2 or micro-rad/s */
#define TOLERANCE_MICRO 10

#define LAST_TS_NS 1000000000ULL

/* Accel FIFO frame headers, with the interrupt tag bits cleared */
#define ACC_FIFO_ACCEL  0x84
#define ACC_FIFO_SKIP   0x40
#define ACC_FIFO_TIME   0x44
#define ACC_FIFO_CONFIG 0x48
#define ACC_FIFO_DROP   0x50
#define ACC_FIFO_EMPTY  0x80

#define MAX_READINGS 8

static const struct sensor_decoder_api *accel_decoder =
	SENSOR_DECODER_DT_GET(DT_NODELABEL(bmi08x_accel));
static const struct sensor_decoder_api *gyro_decoder =
	SENSOR_DECODER_DT_GET(DT_NODELABEL(bmi08x_gyro));

static const struct sensor_chan_spec chan_accel = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_gyro = {SENSOR_CHAN_GYRO_XYZ, 0};

/* Encoded buffers, as allocated by the driver: the header followed by the FIFO data */
static uint8_t accel_buf[sizeof(struct bmi08x_accel_encoded_data) + 64] __aligned(8);
static uint8_t gyro_buf[sizeof(struct bmi08x_gyro_encoded_data) + 64] __aligned(8);

static struct bmi08x_accel_encoded_data *const accel_edata =
	(struct bmi08x_accel_encoded_data *)accel_buf;
static struct bmi08x_gyro_encoded_data *const gyro_edata =
	(struct bmi08x_gyro_encoded_data *)gyro_buf;

static struct {
	struct sensor_three_axis_data data;
	struct sensor_three_axis_sample_data extra[MAX_READINGS - 1];
} out;

/* Readings of out, all MAX_READINGS of them */
static const struct sensor_three_axis_sample_data *const readings = out.data.readings;

struct xyz {
	int16_t x;
	int16_t y;
	int16_t z;
};

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Full scale in g: 2, 4, 8, 16 g for the BMI085 and 3, 6, 12, 24 g for the BMI088 */
static int64_t accel_raw_to_micro(int16_t raw, uint8_t chip_id, uint8_t range)
{
	int64_t fs_g = ((chip_id == BMI085_ACCEL_CHIP_ID) ? 2 : 3) << range;

	return (int64_t)raw * fs_g * SENSOR_G / 32768;
}

/* Full scale in dps: 2000, 1000, 500, 250, 125 dps */
static int64_t gyro_raw_to_micro(int16_t raw, uint8_t range)
{
	int64_t fs_dps = 2000 >> range;

	return (int64_t)raw * fs_dps * SENSOR_PI / 180 / 32768;
}

static void check_value(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);
}

static void check_accel(const struct sensor_three_axis_sample_data *reading, int8_t shift,
			const struct xyz *raw, uint8_t chip_id, uint8_t range)
{
	check_value(reading->x, shift, accel_raw_to_micro(raw->x, chip_id, range));
	check_value(reading->y, shift, accel_raw_to_micro(raw->y, chip_id, range));
	check_value(reading->z, shift, accel_raw_to_micro(raw->z, chip_id, range));
}

static void check_gyro(const struct sensor_three_axis_sample_data *reading, int8_t shift,
		       const struct xyz *raw, uint8_t range)
{
	check_value(reading->x, shift, gyro_raw_to_micro(raw->x, range));
	check_value(reading->y, shift, gyro_raw_to_micro(raw->y, range));
	check_value(reading->z, shift, gyro_raw_to_micro(raw->z, range));
}

static void put_xyz(uint8_t *dst, const struct xyz *raw)
{
	sys_put_le16((uint16_t)raw->x, &dst[0]);
	sys_put_le16((uint16_t)raw->y, &dst[2]);
	sys_put_le16((uint16_t)raw->z, &dst[4]);
}

static uint64_t reading_ts(int idx)
{
	return out.data.header.base_timestamp_ns + readings[idx].timestamp_delta;
}

static void check_unsupported(const struct sensor_decoder_api *decoder, const uint8_t *buffer,
			      struct sensor_chan_spec chan)
{
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_size_info(const struct sensor_decoder_api *decoder, struct sensor_chan_spec chan)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
}

ZTEST(bmi08x_decoder, test_size_info)
{
	check_size_info(accel_decoder, chan_accel);
	check_size_info(gyro_decoder, chan_gyro);
}

static void accel_one_shot(uint8_t chip_id, uint8_t range, const struct xyz *raw)
{
	const uint8_t *buffer = accel_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	memset(accel_buf, 0, sizeof(accel_buf));
	accel_edata->header.timestamp = LAST_TS_NS;
	accel_edata->header.has_accel = true;
	accel_edata->header.chip_id = chip_id;
	accel_edata->header.range = range;
	accel_edata->header.sample_count = 1;
	accel_edata->header.accel_odr = BMI08X_ACCEL_ODR_100_HZ;
	put_xyz((uint8_t *)accel_edata->payload, raw);

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.shift, 5 + range);
	zassert_equal(reading_ts(0), LAST_TS_NS);
	check_accel(&readings[0], out.data.shift, raw, chip_id, range);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);

	zassert_false(accel_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(bmi08x_decoder, test_accel_one_shot)
{
	const struct xyz bmi088_3g = {10923, -10923, 0};
	const struct xyz bmi088_24g = {INT16_MAX, INT16_MIN, 1365};
	const struct xyz bmi085_2g = {16384, -1, 1};
	const struct xyz bmi085_16g = {-2048, 2048, INT16_MIN};

	accel_one_shot(BMI088_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_2G_3G, &bmi088_3g);
	accel_one_shot(BMI088_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_16G_24G, &bmi088_24g);
	accel_one_shot(BMI085_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_2G_3G, &bmi085_2g);
	accel_one_shot(BMI085_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_16G_24G, &bmi085_16g);
}

ZTEST(bmi08x_decoder, test_accel_unsupported)
{
	const struct xyz raw = {1, 2, 3};
	uint16_t frame_count;
	uint32_t fit = 0;

	accel_one_shot(BMI088_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_4G_6G, &raw);

	check_unsupported(accel_decoder, accel_buf,
			  (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0});
	check_unsupported(accel_decoder, accel_buf,
			  (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(accel_decoder, accel_buf, chan_gyro);

	accel_edata->header.has_accel = false;
	zassert_equal(accel_decoder->get_frame_count(accel_buf, chan_accel, &frame_count),
		      -ENODATA);
	zassert_equal(accel_decoder->decode(accel_buf, chan_accel, &fit, 1, &out), -ENODATA);
}

#define ACCEL_FIFO_READINGS 4

static const struct xyz accel_fifo_raw[ACCEL_FIFO_READINGS] = {
	{100, -100, 5461},
	{200, -200, 5462},
	{-300, 300, -5461},
	{INT16_MAX, INT16_MIN, 0},
};

static size_t put_accel_frame(uint8_t *fifo, size_t pos, uint8_t tags, const struct xyz *raw)
{
	fifo[pos] = ACC_FIFO_ACCEL | tags;
	put_xyz(&fifo[pos + 1], raw);

	return pos + 7;
}

static size_t put_control_frame(uint8_t *fifo, size_t pos, uint8_t header, size_t len)
{
	fifo[pos] = header;
	for (size_t i = 1; i < len; i++) {
		fifo[pos + i] = 0x5A;
	}

	return pos + len;
}

/*
 * Fill a FIFO buffer as read by the driver: accel frames interleaved with control frames,
 * followed by empty frames returned when reading past the FIFO fill level.
 */
static void accel_fifo_fill(uint8_t odr, uint8_t range)
{
	uint8_t *fifo = accel_edata->fifo;
	size_t pos = 0;

	memset(accel_buf, 0, sizeof(accel_buf));
	accel_edata->header.timestamp = LAST_TS_NS;
	accel_edata->header.has_accel = true;
	accel_edata->header.is_streaming = true;
	accel_edata->header.chip_id = BMI088_ACCEL_CHIP_ID;
	accel_edata->header.range = range;
	accel_edata->header.sample_count = ACCEL_FIFO_READINGS;
	accel_edata->header.accel_odr = odr;
	accel_edata->header.fifo_len = 40;

	pos = put_accel_frame(fifo, pos, 0x00, &accel_fifo_raw[0]);
	pos = put_control_frame(fifo, pos, ACC_FIFO_SKIP, 2);
	pos = put_accel_frame(fifo, pos, 0x01, &accel_fifo_raw[1]);
	pos = put_control_frame(fifo, pos, ACC_FIFO_CONFIG, 2);
	pos = put_control_frame(fifo, pos, ACC_FIFO_DROP, 2);
	pos = put_accel_frame(fifo, pos, 0x02, &accel_fifo_raw[2]);
	pos = put_control_frame(fifo, pos, ACC_FIFO_TIME, 4);
	pos = put_accel_frame(fifo, pos, 0x03, &accel_fifo_raw[3]);
	pos = put_control_frame(fifo, pos, ACC_FIFO_EMPTY, 2);
	pos = put_control_frame(fifo, pos, ACC_FIFO_EMPTY, 2);
	accel_edata->header.buf_len = pos;
}

static void accel_fifo_check(uint8_t odr, uint64_t period_ns)
{
	const uint8_t *buffer = accel_buf;
	const uint8_t range = BMI08X_ACC_RANGE_8G_12G;
	uint16_t frame_count;
	uint32_t fit = 0;

	accel_fifo_fill(odr, range);

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, ACCEL_FIFO_READINGS);

	/* All readings in one call */
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out),
		      ACCEL_FIFO_READINGS);
	zassert_equal(out.data.header.reading_count, ACCEL_FIFO_READINGS);
	zassert_equal(out.data.shift, 7);
	for (int i = 0; i < ACCEL_FIFO_READINGS; i++) {
		zassert_equal(reading_ts(i),
			      LAST_TS_NS - (ACCEL_FIFO_READINGS - 1 - i) * period_ns);
		check_accel(&readings[i], out.data.shift, &accel_fifo_raw[i], BMI088_ACCEL_CHIP_ID,
			    range);
	}
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < ACCEL_FIFO_READINGS; i++) {
		zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, 1, &out), 1);
		zassert_equal(out.data.header.reading_count, 1);
		zassert_equal(reading_ts(0),
			      LAST_TS_NS - (ACCEL_FIFO_READINGS - 1 - i) * period_ns);
		check_accel(&readings[0], out.data.shift, &accel_fifo_raw[i], BMI088_ACCEL_CHIP_ID,
			    range);
	}
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, 1, &out), 0);
}

ZTEST(bmi08x_decoder, test_accel_fifo)
{
	accel_fifo_check(BMI08X_ACCEL_ODR_100_HZ, 10000000ULL);
	accel_fifo_check(BMI08X_ACCEL_ODR_12_5_HZ, 80000000ULL);
	accel_fifo_check(BMI08X_ACCEL_ODR_1600_HZ, 625000ULL);
}

ZTEST(bmi08x_decoder, test_accel_fifo_above_watermark)
{
	const uint8_t *buffer = accel_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* The driver reads one frame more than the watermark: the newest one has the timestamp */
	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	accel_edata->header.sample_count = ACCEL_FIFO_READINGS - 1;

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, ACCEL_FIFO_READINGS);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, 0, &out), 0);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out),
		      ACCEL_FIFO_READINGS);
	zassert_equal(reading_ts(0), LAST_TS_NS - (ACCEL_FIFO_READINGS - 1) * 10000000ULL);
	zassert_equal(reading_ts(ACCEL_FIFO_READINGS - 1), LAST_TS_NS);
}

ZTEST(bmi08x_decoder, test_accel_fifo_partial_frame)
{
	const uint8_t *buffer = accel_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* A truncated accel frame at the end of the buffer is ignored */
	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	accel_edata->header.buf_len -= 4;
	(void)put_accel_frame(accel_edata->fifo, accel_edata->header.buf_len, 0x00,
			      &accel_fifo_raw[0]);
	accel_edata->header.buf_len += 4;

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, ACCEL_FIFO_READINGS);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out),
		      ACCEL_FIFO_READINGS);
	zassert_equal(reading_ts(ACCEL_FIFO_READINGS - 1), LAST_TS_NS);
	check_accel(&readings[ACCEL_FIFO_READINGS - 1], out.data.shift,
		    &accel_fifo_raw[ACCEL_FIFO_READINGS - 1], BMI088_ACCEL_CHIP_ID,
		    BMI08X_ACC_RANGE_2G_3G);
}

ZTEST(bmi08x_decoder, test_accel_fifo_invalid_header)
{
	const uint8_t *buffer = accel_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Decoding stops at an invalid frame header, the frames before it are kept */
	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	accel_edata->fifo[7] = 0x10;

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 1);
	check_accel(&readings[0], out.data.shift, &accel_fifo_raw[0], BMI088_ACCEL_CHIP_ID,
		    BMI08X_ACC_RANGE_2G_3G);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);

	/* Invalid header in the first frame */
	accel_edata->fifo[0] = 0x10;
	fit = 0;
	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);
}

ZTEST(bmi08x_decoder, test_accel_fifo_no_accel_frame)
{
	const uint8_t *buffer = accel_buf;
	uint16_t frame_count;
	uint32_t fit = 0;
	size_t pos = 0;

	/* A FIFO holding control frames only has no reading of the channel */
	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	pos = put_control_frame(accel_edata->fifo, pos, ACC_FIFO_SKIP, 2);
	pos = put_control_frame(accel_edata->fifo, pos, ACC_FIFO_TIME, 4);
	pos = put_control_frame(accel_edata->fifo, pos, ACC_FIFO_EMPTY, 2);
	accel_edata->header.buf_len = pos;

	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out),
		      -ENODATA);

	/* An empty FIFO buffer has no frame */
	accel_edata->header.buf_len = 0;
	zassert_ok(accel_decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out), 0);
}

ZTEST(bmi08x_decoder, test_accel_fifo_zero_timestamp)
{
	const uint8_t *buffer = accel_buf;
	uint32_t fit = 0;
	int total = 0;
	int ret;

	/*
	 * With a buffer timestamp of 0, as encoded when the sensor clock cannot be read, the
	 * timestamps of the older readings wrap: all readings are still decoded, in order.
	 */
	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	accel_edata->header.timestamp = 0;

	while ((ret = accel_decoder->decode(buffer, chan_accel, &fit, MAX_READINGS, &out)) > 0) {
		for (int i = 0; i < ret; i++) {
			zassert_true(total + i < ACCEL_FIFO_READINGS);
			zassert_equal(reading_ts(i),
				      0ULL - (ACCEL_FIFO_READINGS - 1 - total - i) * 10000000ULL);
			check_accel(&readings[i], out.data.shift, &accel_fifo_raw[total + i],
				    BMI088_ACCEL_CHIP_ID, BMI08X_ACC_RANGE_2G_3G);
		}
		total += ret;
	}
	zassert_equal(ret, 0);
	zassert_equal(total, ACCEL_FIFO_READINGS);
}

ZTEST(bmi08x_decoder, test_accel_fifo_trigger)
{
	const uint8_t *buffer = accel_buf;

	accel_fifo_fill(BMI08X_ACCEL_ODR_100_HZ, BMI08X_ACC_RANGE_2G_3G);
	zassert_true(accel_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(accel_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(accel_decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	accel_edata->header.fifo_len = 0;
	zassert_false(accel_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

static void gyro_one_shot(uint8_t range, const struct xyz *raw)
{
	const uint8_t *buffer = gyro_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	memset(gyro_buf, 0, sizeof(gyro_buf));
	gyro_edata->header.timestamp = LAST_TS_NS;
	gyro_edata->header.has_gyro = true;
	gyro_edata->header.range = range;
	gyro_edata->header.sample_count = 1;
	gyro_edata->header.gyro_odr = BMI08X_GYRO_BW_116_ODR_1000_HZ;
	/* Not written for single samples */
	gyro_edata->header.int_status = 0xFF;
	gyro_edata->header.fifo_status = 0x00;
	put_xyz((uint8_t *)&gyro_edata->frame, raw);

	zassert_ok(gyro_decoder->get_frame_count(buffer, chan_gyro, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, MAX_READINGS, &out), 1);
	zassert_equal(out.data.header.reading_count, 1);
	zassert_equal(out.data.shift, 6 - range);
	zassert_equal(reading_ts(0), LAST_TS_NS);
	check_gyro(&readings[0], out.data.shift, raw, range);
	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, MAX_READINGS, &out), 0);

	zassert_false(gyro_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(bmi08x_decoder, test_gyro_one_shot)
{
	const struct xyz raw_2000 = {INT16_MAX, INT16_MIN, 16};
	const struct xyz raw_1000 = {32, -32, 1000};
	const struct xyz raw_125 = {-1, 1, INT16_MIN};

	gyro_one_shot(BMI08X_GYR_RANGE_2000DPS, &raw_2000);
	gyro_one_shot(BMI08X_GYR_RANGE_1000DPS, &raw_1000);
	gyro_one_shot(BMI08X_GYR_RANGE_500DPS, &raw_1000);
	gyro_one_shot(BMI08X_GYR_RANGE_250DPS, &raw_1000);
	gyro_one_shot(BMI08X_GYR_RANGE_125DPS, &raw_125);
}

/*
 * q31 values of a single sample, as computed by the driver with 64-bit arithmetic before it
 * used the decoder helpers. They do not depend on the range: only the shift does.
 */
static q31_t accel_q31_reference(int16_t raw, uint8_t chip_id)
{
	int64_t fs_g = (chip_id == BMI085_ACCEL_CHIP_ID) ? 2 : 3;

	return (q31_t)(((int64_t)raw * fs_g * 2048) * SENSOR_G / 1000000);
}

static q31_t gyro_q31_reference(int16_t raw)
{
	return (q31_t)(((int64_t)raw * 2000 * 1024) * SENSOR_PI / 1000000 / 180);
}

ZTEST(bmi08x_decoder, test_q31_bit_exact)
{
	static const uint8_t chip_ids[] = {BMI085_ACCEL_CHIP_ID, BMI088_ACCEL_CHIP_ID};

	for (int32_t raw = INT16_MIN; raw <= INT16_MAX; raw++) {
		const struct xyz xyz = {raw, -raw - 1, raw};
		uint32_t fit;

		for (size_t i = 0; i < ARRAY_SIZE(chip_ids); i++) {
			memset(accel_buf, 0, sizeof(accel_buf));
			accel_edata->header.has_accel = true;
			accel_edata->header.chip_id = chip_ids[i];
			accel_edata->header.sample_count = 1;
			put_xyz((uint8_t *)accel_edata->payload, &xyz);

			fit = 0;
			zassert_equal(accel_decoder->decode(accel_buf, chan_accel, &fit, 1, &out),
				      1);
			zassert_equal(readings[0].x, accel_q31_reference(xyz.x, chip_ids[i]));
			zassert_equal(readings[0].y, accel_q31_reference(xyz.y, chip_ids[i]));
		}

		memset(gyro_buf, 0, sizeof(gyro_buf));
		gyro_edata->header.has_gyro = true;
		gyro_edata->header.sample_count = 1;
		put_xyz((uint8_t *)&gyro_edata->frame, &xyz);

		fit = 0;
		zassert_equal(gyro_decoder->decode(gyro_buf, chan_gyro, &fit, 1, &out), 1);
		zassert_equal(readings[0].x, gyro_q31_reference(xyz.x));
		zassert_equal(readings[0].y, gyro_q31_reference(xyz.y));
	}
}

ZTEST(bmi08x_decoder, test_gyro_unsupported)
{
	const struct xyz raw = {1, 2, 3};
	uint16_t frame_count;
	uint32_t fit = 0;

	gyro_one_shot(BMI08X_GYR_RANGE_2000DPS, &raw);

	check_unsupported(gyro_decoder, gyro_buf, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0});
	check_unsupported(gyro_decoder, gyro_buf,
			  (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 1});
	check_unsupported(gyro_decoder, gyro_buf, chan_accel);

	gyro_edata->header.has_gyro = false;
	zassert_equal(gyro_decoder->get_frame_count(gyro_buf, chan_gyro, &frame_count), -ENODATA);
	zassert_equal(gyro_decoder->decode(gyro_buf, chan_gyro, &fit, 1, &out), -ENODATA);
}

#define GYRO_FIFO_WM 4

static const struct xyz gyro_fifo_raw[GYRO_FIFO_WM] = {
	{1000, -1000, 0},
	{2000, -2000, 1},
	{-3000, 3000, INT16_MAX},
	{4000, -4000, INT16_MIN},
};

/* Fill a FIFO buffer with GYRO_FIFO_WM frames; fifo_status holds the FIFO frame count */
static void gyro_fifo_fill(uint8_t odr, uint8_t fifo_status)
{
	uint8_t *fifo = (uint8_t *)gyro_edata->fifo;

	memset(gyro_buf, 0, sizeof(gyro_buf));
	gyro_edata->header.timestamp = LAST_TS_NS;
	gyro_edata->header.has_gyro = true;
	gyro_edata->header.is_streaming = true;
	gyro_edata->header.range = BMI08X_GYR_RANGE_1000DPS;
	gyro_edata->header.sample_count = GYRO_FIFO_WM;
	gyro_edata->header.gyro_odr = odr;
	gyro_edata->header.int_status = BIT(4);
	gyro_edata->header.fifo_status = fifo_status;

	for (int i = 0; i < GYRO_FIFO_WM; i++) {
		put_xyz(&fifo[i * 6], &gyro_fifo_raw[i]);
	}
}

static void gyro_fifo_check(uint8_t odr, uint64_t period_ns, uint8_t fifo_status, int count)
{
	const uint8_t *buffer = gyro_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	gyro_fifo_fill(odr, fifo_status);

	zassert_ok(gyro_decoder->get_frame_count(buffer, chan_gyro, &frame_count));
	zassert_equal(frame_count, count);

	/* All readings in one call */
	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, MAX_READINGS, &out), count);
	zassert_equal(out.data.header.reading_count, count);
	zassert_equal(out.data.shift, 5);
	for (int i = 0; i < count; i++) {
		zassert_equal(reading_ts(i), LAST_TS_NS - (count - 1 - i) * period_ns);
		check_gyro(&readings[i], out.data.shift, &gyro_fifo_raw[i],
			   BMI08X_GYR_RANGE_1000DPS);
	}
	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, MAX_READINGS, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < count; i++) {
		zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, 1, &out), 1);
		zassert_equal(reading_ts(0), LAST_TS_NS - (count - 1 - i) * period_ns);
		check_gyro(&readings[0], out.data.shift, &gyro_fifo_raw[i],
			   BMI08X_GYR_RANGE_1000DPS);
	}
	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, 1, &out), 0);
}

ZTEST(bmi08x_decoder, test_gyro_fifo)
{
	/* FIFO holding more frames than the watermark, with the overrun bit set */
	gyro_fifo_check(BMI08X_GYRO_BW_116_ODR_1000_HZ, 1000000ULL, 0x80 | 10, GYRO_FIFO_WM);
	gyro_fifo_check(BMI08X_GYRO_BW_532_ODR_2000_HZ, 500000ULL, GYRO_FIFO_WM, GYRO_FIFO_WM);
	gyro_fifo_check(BMI08X_GYRO_BW_47_ODR_400_HZ, 2500000ULL, GYRO_FIFO_WM, GYRO_FIFO_WM);
	gyro_fifo_check(BMI08X_GYRO_BW_32_ODR_100_HZ, 10000000ULL, GYRO_FIFO_WM, GYRO_FIFO_WM);
	/* Fewer frames in the FIFO than the watermark */
	gyro_fifo_check(BMI08X_GYRO_BW_64_ODR_200_HZ, 5000000ULL, 2, 2);
}

ZTEST(bmi08x_decoder, test_gyro_fifo_empty)
{
	const uint8_t *buffer = gyro_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	gyro_fifo_fill(BMI08X_GYRO_BW_116_ODR_1000_HZ, 0x00);

	zassert_ok(gyro_decoder->get_frame_count(buffer, chan_gyro, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(gyro_decoder->decode(buffer, chan_gyro, &fit, MAX_READINGS, &out), 0);
	zassert_false(gyro_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(bmi08x_decoder, test_gyro_fifo_trigger)
{
	const uint8_t *buffer = gyro_buf;

	gyro_fifo_fill(BMI08X_GYRO_BW_116_ODR_1000_HZ, GYRO_FIFO_WM);
	zassert_true(gyro_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(gyro_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	gyro_edata->header.int_status = 0;
	zassert_false(gyro_decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST_SUITE(bmi08x_decoder, NULL, NULL, NULL, NULL, NULL);
