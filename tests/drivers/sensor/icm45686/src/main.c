/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "icm45686.h"
#include "icm45686_reg.h"

/* Allowed error of a decoded value, in micro-units */
#define TOLERANCE_MICRO 50

#define TIMESTAMP_NS   5000000000ULL
#define NUM_FRAMES     4
#define NS_PER_HW_TICK 16000ULL

#define FIFO_HEADER                                                                                \
	(FIFO_HEADER_ACCEL_EN(true) | FIFO_HEADER_GYRO_EN(true) | FIFO_HEADER_HIRES_EN(true))

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(icm45686));

static const struct sensor_chan_spec accel_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec gyro_xyz = {SENSOR_CHAN_GYRO_XYZ, 0};
static const struct sensor_chan_spec die_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const uint32_t accel_fs_g[] = {
	[ICM45686_DT_ACCEL_FS_32] = 32, [ICM45686_DT_ACCEL_FS_16] = 16,
	[ICM45686_DT_ACCEL_FS_8] = 8,   [ICM45686_DT_ACCEL_FS_4] = 4,
	[ICM45686_DT_ACCEL_FS_2] = 2,
};

static const int8_t accel_shift[] = {
	[ICM45686_DT_ACCEL_FS_32] = 9, [ICM45686_DT_ACCEL_FS_16] = 8, [ICM45686_DT_ACCEL_FS_8] = 7,
	[ICM45686_DT_ACCEL_FS_4] = 6,  [ICM45686_DT_ACCEL_FS_2] = 5,
};

static const uint32_t gyro_fs_mdps[] = {
	[ICM45686_DT_GYRO_FS_4000] = 4000000, [ICM45686_DT_GYRO_FS_2000] = 2000000,
	[ICM45686_DT_GYRO_FS_1000] = 1000000, [ICM45686_DT_GYRO_FS_500] = 500000,
	[ICM45686_DT_GYRO_FS_250] = 250000,   [ICM45686_DT_GYRO_FS_125] = 125000,
	[ICM45686_DT_GYRO_FS_62_5] = 62500,   [ICM45686_DT_GYRO_FS_31_25] = 31250,
	[ICM45686_DT_GYRO_FS_15_625] = 15625,
};

static const int8_t gyro_shift[] = {
	[ICM45686_DT_GYRO_FS_4000] = 12,  [ICM45686_DT_GYRO_FS_2000] = 11,
	[ICM45686_DT_GYRO_FS_1000] = 10,  [ICM45686_DT_GYRO_FS_500] = 9,
	[ICM45686_DT_GYRO_FS_250] = 8,    [ICM45686_DT_GYRO_FS_125] = 7,
	[ICM45686_DT_GYRO_FS_62_5] = 6,   [ICM45686_DT_GYRO_FS_31_25] = 5,
	[ICM45686_DT_GYRO_FS_15_625] = 4,
};

/* Samples of the single sample buffers */
static const int16_t accel_raw[3] = {16384, -12345, 32767};
static const int16_t gyro_raw[3] = {-32768, 1000, 16384};
static const int16_t temp_raw = 1325;

/* Encoded single sample, or header followed by FIFO packets */
static union {
	struct icm45686_encoded_data edata;
	uint8_t bytes[sizeof(struct icm45686_encoded_data) +
		      NUM_FRAMES * sizeof(struct icm45686_encoded_fifo_payload)];
} buf;

static const uint8_t *const buffer = buf.bytes;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Expected values in micro-m/s^2, micro-rad/s and micro-degC */
static int64_t accel_micro(int32_t raw, uint8_t bits, uint8_t fs)
{
	return (int64_t)raw * accel_fs_g[fs] * SENSOR_G / (1LL << (bits - 1));
}

static int64_t gyro_micro(int32_t raw, uint8_t bits, uint8_t fs)
{
	return (int64_t)raw * gyro_fs_mdps[fs] * SENSOR_PI / (180000LL << (bits - 1));
}

static int64_t temp_micro(int16_t raw)
{
	return (int64_t)raw * 100000000 / 13248 + 25000000;
}

static void check_value(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void fill_single(uint8_t accel_fs, uint8_t gyro_fs, uint8_t events)
{
	uint8_t *payload = buf.edata.payload.buf;

	memset(&buf, 0, sizeof(buf));
	buf.edata.header.timestamp = TIMESTAMP_NS;
	buf.edata.header.accel_fs = accel_fs;
	buf.edata.header.gyro_fs = gyro_fs;
	buf.edata.header.events = events;
	buf.edata.header.channels = BIT_MASK(7);

	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)accel_raw[i], &payload[i * 2]);
		sys_put_le16((uint16_t)gyro_raw[i], &payload[6 + i * 2]);
	}
	sys_put_le16((uint16_t)temp_raw, &payload[12]);
}

/* Write a 20-bit sample: MSB word at msb_offset, low nibble in the LSB byte */
static void put_20bit(uint8_t *frame, size_t msb_offset, uint8_t *lsb, int32_t raw,
		      bool high_nibble)
{
	sys_put_le16((uint16_t)((uint32_t)raw >> 4), &frame[msb_offset]);
	*lsb |= high_nibble ? ((raw & 0xF) << 4) : (raw & 0xF);
}

static uint8_t *fifo_frame(int i)
{
	return buf.edata.fifo_payload[i].buf;
}

static void put_fifo_frame(int i, const int32_t accel[3], const int32_t gyro[3], int16_t temp,
			   uint16_t hw_ts)
{
	uint8_t *frame = fifo_frame(i);
	uint8_t *lsb = &frame[offsetof(struct icm45686_encoded_fifo_payload, lsb)];

	memset(frame, 0, sizeof(struct icm45686_encoded_fifo_payload));
	frame[0] = FIFO_HEADER;
	for (int a = 0; a < 3; a++) {
		put_20bit(frame, 1 + a * 2, &lsb[a], accel[a], true);
		put_20bit(frame, 7 + a * 2, &lsb[a], gyro[a], false);
	}
	sys_put_le16((uint16_t)temp, &frame[13]);
	sys_put_le16(hw_ts, &frame[15]);
}

static int32_t fifo_accel(int i, int a)
{
	return (i * 100000 + a * 1111) * ((a == 1) ? -1 : 1) + 7;
}

static int32_t fifo_gyro(int i, int a)
{
	return -(i * 120000 + a * 3333) * ((a == 2) ? -1 : 1) - 5;
}

static int16_t fifo_temp(int i)
{
	/* Down to below 0 degC */
	return (int16_t)(1000 - i * 2000);
}

static uint16_t fifo_hw_ts(int i, uint16_t first, uint16_t step)
{
	return (uint16_t)(first + i * step);
}

/* Fill FIFO frames with hardware timestamps first + i * step */
static void fill_fifo(uint8_t events, uint16_t first, uint16_t step)
{
	memset(&buf, 0, sizeof(buf));
	buf.edata.header.timestamp = TIMESTAMP_NS;
	buf.edata.header.accel_fs = ICM45686_DT_ACCEL_FS_32;
	buf.edata.header.gyro_fs = ICM45686_DT_GYRO_FS_4000;
	buf.edata.header.events = events;
	buf.edata.header.channels = BIT_MASK(7);
	buf.edata.header.fifo_count = NUM_FRAMES;

	for (int i = 0; i < NUM_FRAMES; i++) {
		int32_t accel[3];
		int32_t gyro[3];

		for (int a = 0; a < 3; a++) {
			accel[a] = fifo_accel(i, a);
			gyro[a] = fifo_gyro(i, a);
		}
		put_fifo_frame(i, accel, gyro, fifo_temp(i), fifo_hw_ts(i, first, step));
	}
}

static uint64_t fifo_timestamp(int i, uint16_t step)
{
	return TIMESTAMP_NS - (uint64_t)(NUM_FRAMES - 1 - i) * step * NS_PER_HW_TICK;
}

ZTEST(icm45686_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(accel_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(gyro_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0},
					  &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_ok(decoder->get_size_info(die_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

static void check_single_xyz(struct sensor_chan_spec chan, int8_t shift, const int64_t exp[3])
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, shift);
	for (int a = 0; a < 3; a++) {
		check_value(out.readings[0].values[a], out.shift, exp[a]);
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_single_q31(struct sensor_chan_spec chan, int8_t shift, int64_t exp)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, shift);
	check_value(out.readings[0].value, out.shift, exp);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

ZTEST(icm45686_decoder, test_single_accel)
{
	for (uint8_t fs = 0; fs < ARRAY_SIZE(accel_fs_g); fs++) {
		int64_t exp[3];

		fill_single(fs, ICM45686_DT_GYRO_FS_2000, 0);
		for (int a = 0; a < 3; a++) {
			exp[a] = accel_micro(accel_raw[a], 16, fs);
		}

		check_single_xyz(accel_xyz, accel_shift[fs], exp);
		for (int a = 0; a < 3; a++) {
			struct sensor_chan_spec chan = {SENSOR_CHAN_ACCEL_X + a, 0};

			check_single_q31(chan, accel_shift[fs], exp[a]);
		}
	}
}

ZTEST(icm45686_decoder, test_single_gyro)
{
	for (uint8_t fs = 0; fs < ARRAY_SIZE(gyro_fs_mdps); fs++) {
		int64_t exp[3];

		fill_single(ICM45686_DT_ACCEL_FS_16, fs, 0);
		for (int a = 0; a < 3; a++) {
			exp[a] = gyro_micro(gyro_raw[a], 16, fs);
		}

		check_single_xyz(gyro_xyz, gyro_shift[fs], exp);
		for (int a = 0; a < 3; a++) {
			struct sensor_chan_spec chan = {SENSOR_CHAN_GYRO_X + a, 0};

			check_single_q31(chan, gyro_shift[fs], exp[a]);
		}
	}
}

ZTEST(icm45686_decoder, test_single_temp)
{
	fill_single(ICM45686_DT_ACCEL_FS_16, ICM45686_DT_GYRO_FS_2000, 0);
	check_single_q31(die_temp, 9, temp_micro(temp_raw));

	/* Below 0 degC */
	sys_put_le16((uint16_t)-5000, &buf.edata.payload.buf[12]);
	check_single_q31(die_temp, 9, temp_micro(-5000));
}

ZTEST(icm45686_decoder, test_single_channels)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_single(ICM45686_DT_ACCEL_FS_16, ICM45686_DT_GYRO_FS_2000, 0);

	/* Only the accelerometer was read */
	buf.edata.header.channels = BIT_MASK(3);
	zassert_ok(decoder->get_frame_count(buffer, accel_xyz, &frame_count));
	zassert_equal(decoder->get_frame_count(buffer, gyro_xyz, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->get_frame_count(buffer, die_temp, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, die_temp, &fit, 1, &out), -ENODATA);

	/* Only the Y axis was read */
	buf.edata.header.channels = BIT(1);
	zassert_equal(decoder->get_frame_count(buffer, accel_xyz, &frame_count), -ENODATA);
	zassert_ok(decoder->get_frame_count(
		buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0}, &frame_count));
	zassert_equal(frame_count, 1);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(icm45686_decoder, test_data_ready)
{
	uint16_t frame_count;
	int64_t exp[3];

	fill_single(ICM45686_DT_ACCEL_FS_8, ICM45686_DT_GYRO_FS_250, REG_INT1_STATUS0_DRDY(true));
	for (int a = 0; a < 3; a++) {
		exp[a] = accel_micro(accel_raw[a], 16, ICM45686_DT_ACCEL_FS_8);
	}

	check_single_xyz(accel_xyz, 7, exp);
	check_single_q31(die_temp, 9, temp_micro(temp_raw));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_TAP));

	/* Event without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	buf.edata.header.channels = 0;
	zassert_equal(decoder->get_frame_count(buffer, accel_xyz, &frame_count), -ENODATA);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
}

static void check_fifo_xyz(struct sensor_chan_spec chan, uint16_t step)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	bool accel = chan.chan_type == SENSOR_CHAN_ACCEL_XYZ;
	int8_t shift = accel ? 9 : 12;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(decoder->decode(buffer, chan, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES);
	zassert_equal(out.data.header.base_timestamp_ns, fifo_timestamp(0, step));
	zassert_equal(out.data.shift, shift);

	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(out.data.header.base_timestamp_ns + readings[i].timestamp_delta,
			      fifo_timestamp(i, step));
		for (int a = 0; a < 3; a++) {
			check_value(
				readings[i].values[a], shift,
				accel ? accel_micro(fifo_accel(i, a), 20, ICM45686_DT_ACCEL_FS_32)
				      : gyro_micro(fifo_gyro(i, a), 20, ICM45686_DT_GYRO_FS_4000));
		}
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, NUM_FRAMES, &out), 0);

	/* One reading at a time */
	fit = 0;
	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
		zassert_equal(out.data.header.reading_count, 1);
		zassert_equal(out.data.header.base_timestamp_ns + readings[0].timestamp_delta,
			      fifo_timestamp(i, step));
		check_value(readings[0].values[0], shift,
			    accel ? accel_micro(fifo_accel(i, 0), 20, ICM45686_DT_ACCEL_FS_32)
				  : gyro_micro(fifo_gyro(i, 0), 20, ICM45686_DT_GYRO_FS_4000));
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_fifo_temp(uint16_t step)
{
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_q31_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, die_temp, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(decoder->decode(buffer, die_temp, &fit, NUM_FRAMES, &out), NUM_FRAMES);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES);
	zassert_equal(out.data.shift, 9);
	for (int i = 0; i < NUM_FRAMES; i++) {
		zassert_equal(out.data.header.base_timestamp_ns + readings[i].timestamp_delta,
			      fifo_timestamp(i, step));
		check_value(readings[i].temperature, 9, temp_micro(fifo_temp(i)));
	}
	zassert_equal(decoder->decode(buffer, die_temp, &fit, NUM_FRAMES, &out), 0);
}

ZTEST(icm45686_decoder, test_fifo_watermark)
{
	/* 1 kHz, the hardware timestamp wraps between frames 0 and 1 */
	fill_fifo(REG_INT1_STATUS0_FIFO_THS(true), 65500, 62);

	check_fifo_xyz(accel_xyz, 62);
	check_fifo_xyz(gyro_xyz, 62);
	check_fifo_temp(62);

	/* Individual axes are only decoded from single samples */
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(icm45686_decoder, test_fifo_full)
{
	/* The full scale of FIFO packets does not depend on the header */
	fill_fifo(REG_INT1_STATUS0_FIFO_FULL(true), 100, 12500);
	buf.edata.header.accel_fs = ICM45686_DT_ACCEL_FS_2;
	buf.edata.header.gyro_fs = ICM45686_DT_GYRO_FS_125;

	/* 5 Hz: the frames span 600 ms */
	check_fifo_xyz(accel_xyz, 12500);
	check_fifo_xyz(gyro_xyz, 12500);
	check_fifo_temp(12500);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(icm45686_decoder, test_fifo_no_data)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_FRAMES - 1];
	} out;
	const struct sensor_three_axis_sample_data *readings = out.data.readings;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_fifo(REG_INT1_STATUS0_FIFO_THS(true), 1000, 50);

	/* No accelerometer data in frame 1 */
	sys_put_le16(FIFO_NO_DATA, &fifo_frame(1)[3]);

	zassert_ok(decoder->get_frame_count(buffer, accel_xyz, &frame_count));
	zassert_equal(frame_count, NUM_FRAMES);

	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, NUM_FRAMES, &out), NUM_FRAMES - 1);
	zassert_equal(out.data.header.reading_count, NUM_FRAMES - 1);
	zassert_equal(out.data.header.base_timestamp_ns, fifo_timestamp(0, 50));
	zassert_equal(readings[0].timestamp_delta, 0);
	zassert_equal(readings[1].timestamp_delta, 2 * 50 * NS_PER_HW_TICK);
	zassert_equal(readings[2].timestamp_delta, 3 * 50 * NS_PER_HW_TICK);
	check_value(readings[1].x, 9, accel_micro(fifo_accel(2, 0), 20, ICM45686_DT_ACCEL_FS_32));
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, NUM_FRAMES, &out), 0);

	fit = 0;
	for (int i = 0; i < NUM_FRAMES; i++) {
		if (i == 1) {
			continue;
		}
		zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 1);
		zassert_equal(out.data.header.base_timestamp_ns, fifo_timestamp(i, 50));
	}
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 0);

	/* The gyroscope data of frame 1 is valid */
	fit = 0;
	zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, NUM_FRAMES, &out), NUM_FRAMES);
}

ZTEST(icm45686_decoder, test_fifo_bad_packet)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill_fifo(REG_INT1_STATUS0_FIFO_THS(true), 1000, 50);
	fifo_frame(0)[0] |= FIFO_HEADER_EXT_HEADER_EN(true);

	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), -ENOTSUP);
}

ZTEST(icm45686_decoder, test_fifo_empty)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Event without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&buf, 0, sizeof(struct icm45686_encoded_header));
	buf.edata.header.events = REG_INT1_STATUS0_FIFO_THS(true);

	zassert_ok(decoder->get_frame_count(buffer, accel_xyz, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 0);
	zassert_ok(decoder->get_frame_count(buffer, die_temp, &frame_count));
	zassert_equal(frame_count, 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST_SUITE(icm45686_decoder, NULL, NULL, NULL, NULL, NULL);
