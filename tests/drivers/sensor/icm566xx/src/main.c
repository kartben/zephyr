/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "icm566xx.h"

#define HIGH_FSR IS_ENABLED(CONFIG_DT_HAS_INVENSENSE_ICM56686_ENABLED)

/* Register data has 20 bits on the ICM56686, FIFO frames have 20 bits on both parts */
#define REG_BITS  (HIGH_FSR ? 20 : 16)
#define FIFO_BITS 20

/* FIFO frames are at the highest full scale of the part */
#define FIFO_ACCEL_FS (HIGH_FSR ? ICM566XX_DT_ACCEL_FS_32 : ICM566XX_DT_ACCEL_FS_16)
#define FIFO_GYRO_FS  (HIGH_FSR ? ICM566XX_DT_GYRO_FS_4000 : ICM566XX_DT_GYRO_FS_2000)

/* Allowed error of a decoded value, in micro-units */
#define TOLERANCE_MICRO 100

#define TIMESTAMP_NS 1234567890123ULL

/* Header byte of a 20-byte FIFO frame with accel, gyro and timestamp */
#define FIFO_HEADER 0x78

#define FIFO_FRAMES     4
#define FIFO_DATA_BYTES (FIFO_FRAMES * sizeof(struct icm566xx_encoded_fifo_payload))

/* Encoded channel mask of all channels */
#define ALL_CHANNELS 0x7F

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(imu));

static const struct sensor_chan_spec chan_accel = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_gyro = {SENSOR_CHAN_GYRO_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const struct sensor_chan_spec chan_axes[] = {
	{SENSOR_CHAN_ACCEL_X, 0}, {SENSOR_CHAN_ACCEL_Y, 0}, {SENSOR_CHAN_ACCEL_Z, 0},
	{SENSOR_CHAN_GYRO_X, 0},  {SENSOR_CHAN_GYRO_Y, 0},  {SENSOR_CHAN_GYRO_Z, 0},
};

/* Expected shifts, indexed by the ICM566XX_DT_*_FS_* value */
static const int8_t accel_shift[] = {9, 8, 7, 6, 5};
static const int8_t gyro_shift[] = {12, 11, 10, 9, 8, 7, 6, 5, 4};

#define SHIFT_HEADROOM (HIGH_FSR ? 4 : 0)

/* One-shot sample: accel and gyro X/Y/Z, then temperature */
static const int32_t sample_raw[] = {1000, -2000, 32767, -32768, 1234, -5};
static const uint8_t sample_lsb[] = {0x5, 0xA, 0xF, 0x3, 0x0, 0xC};

static uint8_t buffer[sizeof(struct icm566xx_encoded_data) + FIFO_DATA_BYTES] __aligned(8);
static struct icm566xx_encoded_data *const edata = (struct icm566xx_encoded_data *)buffer;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

/* Expected accel value in micro-m/s^2 */
static int64_t accel_micro(int32_t raw, int bits, uint8_t fs)
{
	/* ICM566XX_DT_ACCEL_FS_32 is 32 g, each next value halves the full scale */
	return (int64_t)raw * SENSOR_G * 32 / ((1LL << (bits - 1)) << fs);
}

/* Expected gyro value in micro-rad/s */
static int64_t gyro_micro(int32_t raw, int bits, uint8_t fs)
{
	/* ICM566XX_DT_GYRO_FS_4000 is 4000 dps, each next value halves the full scale */
	return (int64_t)raw * 4000 * SENSOR_PI / 180 / ((1LL << (bits - 1)) << fs);
}

/* Expected temperature in micro-degC: 132.48 LSB/degC, 25 degC at 0 */
static int64_t temp_micro(int16_t raw)
{
	return (int64_t)raw * 100000000 / 13248 + 25000000;
}

static void check_micro(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_micro(value, shift);

	zassert_within(actual, expected, TOLERANCE_MICRO, "got %lld, expected %lld", actual,
		       expected);
}

/* Raw register value of sample_raw[pos], with the EXT_DATA bits on the ICM56686 */
static int32_t sample_value(int pos)
{
	return HIGH_FSR ? sample_raw[pos] * 16 + sample_lsb[pos] : sample_raw[pos];
}

static void fill_one_shot(uint8_t accel_fs, uint8_t gyro_fs, uint8_t channels, int16_t temp)
{
	uint8_t *regs = (uint8_t *)&edata->payload;

	memset(buffer, 0, sizeof(buffer));
	edata->header.timestamp = TIMESTAMP_NS;
	edata->header.accel_fs = accel_fs;
	edata->header.gyro_fs = gyro_fs;
	edata->header.channels = channels;

	for (int i = 0; i < ARRAY_SIZE(sample_raw); i++) {
		sys_put_le16((uint16_t)sample_raw[i], &regs[i * 2]);
	}
	sys_put_le16((uint16_t)temp, &regs[12]);

#if defined(CONFIG_DT_HAS_INVENSENSE_ICM56686_ENABLED)
	for (int i = 0; i < 3; i++) {
		edata->payload.ext_data[i] = sample_lsb[i] | (sample_lsb[i + 3] << 4);
	}
#endif
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_no_data(struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENODATA);
}

ZTEST(icm566xx_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_accel, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(chan_gyro, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	zassert_ok(decoder->get_size_info(chan_temp, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_q31_data));
	zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));

	for (int i = 0; i < ARRAY_SIZE(chan_axes); i++) {
		zassert_ok(decoder->get_size_info(chan_axes[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_LIGHT, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

static void check_one_shot_xyz(struct sensor_chan_spec chan, int8_t shift, uint8_t fs)
{
	const int first = (chan.chan_type == SENSOR_CHAN_ACCEL_XYZ) ? 0 : 3;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, TIMESTAMP_NS);
	zassert_equal(out.shift, shift, "fs %u: shift %d", fs, out.shift);

	for (int i = 0; i < 3; i++) {
		int32_t raw = sample_value(first + i);
		int64_t expected = (first == 0) ? accel_micro(raw, REG_BITS, fs)
						: gyro_micro(raw, REG_BITS, fs);

		check_micro(out.readings[0].values[i], out.shift, expected);
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

static void check_one_shot_axis(int pos, int8_t shift, uint8_t fs)
{
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit = 0;
	int32_t raw = sample_value(pos);
	int64_t expected =
		(pos < 3) ? accel_micro(raw, REG_BITS, fs) : gyro_micro(raw, REG_BITS, fs);

	zassert_ok(decoder->get_frame_count(buffer, chan_axes[pos], &frame_count));
	zassert_equal(frame_count, 1);

	zassert_equal(decoder->decode(buffer, chan_axes[pos], &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta, TIMESTAMP_NS);
	zassert_equal(out.shift, shift);
	check_micro(out.readings[0].value, out.shift, expected);
	zassert_equal(decoder->decode(buffer, chan_axes[pos], &fit, 1, &out), 0);
}

ZTEST(icm566xx_decoder, test_one_shot_accel)
{
	for (uint8_t fs = ICM566XX_DT_ACCEL_FS_32; fs <= ICM566XX_DT_ACCEL_FS_2; fs++) {
		int8_t shift = accel_shift[fs] + SHIFT_HEADROOM;

		fill_one_shot(fs, ICM566XX_DT_GYRO_FS_2000, ALL_CHANNELS, 0);
		check_one_shot_xyz(chan_accel, shift, fs);

		for (int pos = 0; pos < 3; pos++) {
			check_one_shot_axis(pos, shift, fs);
		}
	}
}

ZTEST(icm566xx_decoder, test_one_shot_gyro)
{
	for (uint8_t fs = ICM566XX_DT_GYRO_FS_4000; fs <= ICM566XX_DT_GYRO_FS_15_625; fs++) {
		int8_t shift = gyro_shift[fs] + SHIFT_HEADROOM;

		fill_one_shot(ICM566XX_DT_ACCEL_FS_16, fs, ALL_CHANNELS, 0);
		check_one_shot_xyz(chan_gyro, shift, fs);

		for (int pos = 3; pos < 6; pos++) {
			check_one_shot_axis(pos, shift, fs);
		}
	}
}

ZTEST(icm566xx_decoder, test_one_shot_temp)
{
	static const int16_t temps[] = {0, 13248, -6624, -32768, 32767};
	struct sensor_q31_data out;
	uint16_t frame_count;
	uint32_t fit;

	for (int i = 0; i < ARRAY_SIZE(temps); i++) {
		fill_one_shot(ICM566XX_DT_ACCEL_FS_16, ICM566XX_DT_GYRO_FS_2000, ALL_CHANNELS,
			      temps[i]);

		zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
		zassert_equal(frame_count, 1);

		fit = 0;
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
		zassert_equal(out.header.reading_count, 1);
		zassert_equal(out.header.base_timestamp_ns + out.readings[0].timestamp_delta,
			      TIMESTAMP_NS);
		zassert_equal(out.shift, 9);
		check_micro(out.readings[0].temperature, out.shift, temp_micro(temps[i]));
	}
}

ZTEST(icm566xx_decoder, test_one_shot_channels)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* Only the accelerometer was requested */
	fill_one_shot(ICM566XX_DT_ACCEL_FS_16, ICM566XX_DT_GYRO_FS_2000, 0x07, 0);

	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), 1);
	check_no_data(chan_gyro);
	check_no_data(chan_temp);
	check_no_data(chan_axes[3]);

	/* Only the accelerometer Y axis was requested */
	fill_one_shot(ICM566XX_DT_ACCEL_FS_16, ICM566XX_DT_GYRO_FS_2000, BIT(1), 0);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_axes[1], &fit, 1, &out), 1);
	check_no_data(chan_accel);
	check_no_data(chan_axes[0]);
}

ZTEST(icm566xx_decoder, test_one_shot_errors)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fill_one_shot(ICM566XX_DT_ACCEL_FS_16, ICM566XX_DT_GYRO_FS_2000, ALL_CHANNELS, 0);

	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 0, &out), 0);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_LIGHT, 0});

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	/* Invalid full scale values */
	fill_one_shot(ICM566XX_DT_ACCEL_FS_2 + 1, ICM566XX_DT_GYRO_FS_15_625 + 1, ALL_CHANNELS, 0);
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 1, &out), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, &out), 1);
}

ZTEST(icm566xx_decoder, test_data_ready)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	/* Stream buffer for a data ready trigger with SENSOR_STREAM_DATA_INCLUDE */
	fill_one_shot(ICM566XX_DT_ACCEL_FS_16, ICM566XX_DT_GYRO_FS_2000, ALL_CHANNELS, 0);
	edata->header.events = BIT(0);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));

	zassert_ok(decoder->get_frame_count(buffer, chan_accel, &frame_count));
	zassert_equal(frame_count, 1);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	check_micro(out.readings[0].x, out.shift,
		    accel_micro(sample_value(0), REG_BITS, ICM566XX_DT_ACCEL_FS_16));
}

/* FIFO frame values, 20 bits */
static int32_t fifo_accel(int frame, int axis)
{
	static const int32_t base[] = {7, -3, 524287};

	return base[axis] + ((axis == 2) ? -frame : frame * (1000 - 3000 * axis));
}

static int32_t fifo_gyro(int frame, int axis)
{
	static const int32_t base[] = {-524000, 9, -9};

	return base[axis] + ((axis == 0) ? frame : frame * (333 - 4777 * axis));
}

static int16_t fifo_temp(int frame)
{
	return -5000 + frame * 3000;
}

static void fill_fifo(uint8_t events)
{
	memset(buffer, 0, sizeof(buffer));
	edata->header.timestamp = TIMESTAMP_NS;
	edata->header.events = events;
	/* As set by the driver for FIFO data */
	edata->header.accel_fs = ICM566XX_DT_ACCEL_FS_32;
	edata->header.gyro_fs = ICM566XX_DT_GYRO_FS_4000;
	edata->header.channels = ALL_CHANNELS;
	edata->header.fifo_count = FIFO_FRAMES;

	for (int i = 0; i < FIFO_FRAMES; i++) {
		uint8_t *frame = edata->fifo_payload[i].buf;

		frame[0] = FIFO_HEADER;
		for (int axis = 0; axis < 3; axis++) {
			int32_t accel = fifo_accel(i, axis);
			int32_t gyro = fifo_gyro(i, axis);

			sys_put_le16((uint16_t)(accel >> 4), &frame[1 + axis * 2]);
			sys_put_le16((uint16_t)(gyro >> 4), &frame[7 + axis * 2]);
			frame[17 + axis] = ((accel & 0xF) << 4) | (gyro & 0xF);
		}
		sys_put_le16((uint16_t)fifo_temp(i), &frame[13]);
		sys_put_le16(0x1234 + i, &frame[15]);
	}
}

/* Mark a sensor as without data in a FIFO frame */
static void fifo_no_data(int frame, int offset)
{
	sys_put_le16(FIFO_NO_DATA, &edata->fifo_payload[frame].buf[offset]);
}

static void check_fifo_xyz(struct sensor_chan_spec chan, const int *frames, int count)
{
	const bool accel = chan.chan_type == SENSOR_CHAN_ACCEL_XYZ;
	const int8_t shift = accel ? accel_shift[FIFO_ACCEL_FS] + SHIFT_HEADROOM
				   : gyro_shift[FIFO_GYRO_FS] + SHIFT_HEADROOM;
	uint8_t out_buf[sizeof(struct sensor_three_axis_data) +
			(FIFO_FRAMES - 1) * sizeof(struct sensor_three_axis_sample_data)]
		__aligned(8);
	struct sensor_three_axis_data *out = (struct sensor_three_axis_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, count);

	/* All readings at once */
	zassert_equal(decoder->decode(buffer, chan, &fit, FIFO_FRAMES, out), count);
	zassert_equal(out->header.reading_count, count);
	zassert_equal(out->shift, shift);

	for (int i = 0; i < count; i++) {
		const struct sensor_three_axis_sample_data *reading = &out->readings[i];

		zassert_equal(out->header.base_timestamp_ns + reading->timestamp_delta,
			      TIMESTAMP_NS);

		for (int axis = 0; axis < 3; axis++) {
			int64_t expected = accel ? accel_micro(fifo_accel(frames[i], axis),
							       FIFO_BITS, FIFO_ACCEL_FS)
						 : gyro_micro(fifo_gyro(frames[i], axis), FIFO_BITS,
							      FIFO_GYRO_FS);

			check_micro(reading->values[axis], out->shift, expected);
		}
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, FIFO_FRAMES, out), 0);

	/* One reading at a time */
	fit = 0;
	for (int i = 0; i < count; i++) {
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, out), 1);
		zassert_equal(out->header.reading_count, 1);
		zassert_equal(out->header.base_timestamp_ns + out->readings[0].timestamp_delta,
			      TIMESTAMP_NS);
		check_micro(out->readings[0].x, out->shift,
			    accel ? accel_micro(fifo_accel(frames[i], 0), FIFO_BITS, FIFO_ACCEL_FS)
				  : gyro_micro(fifo_gyro(frames[i], 0), FIFO_BITS, FIFO_GYRO_FS));
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, out), 0);
}

static void check_fifo_temp(const int *frames, int count)
{
	uint8_t out_buf[sizeof(struct sensor_q31_data) +
			(FIFO_FRAMES - 1) * sizeof(struct sensor_q31_sample_data)] __aligned(8);
	struct sensor_q31_data *out = (struct sensor_q31_data *)out_buf;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, chan_temp, &frame_count));
	zassert_equal(frame_count, count);

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, FIFO_FRAMES, out), count);
	zassert_equal(out->header.reading_count, count);
	zassert_equal(out->shift, 9);

	for (int i = 0; i < count; i++) {
		zassert_equal(out->header.base_timestamp_ns + out->readings[i].timestamp_delta,
			      TIMESTAMP_NS);
		check_micro(out->readings[i].temperature, out->shift,
			    temp_micro(fifo_temp(frames[i])));
	}

	zassert_equal(decoder->decode(buffer, chan_temp, &fit, FIFO_FRAMES, out), 0);

	fit = 0;
	for (int i = 0; i < count; i++) {
		zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, out), 1);
		check_micro(out->readings[0].temperature, out->shift,
			    temp_micro(fifo_temp(frames[i])));
	}
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 1, out), 0);
}

ZTEST(icm566xx_decoder, test_fifo)
{
	static const int all[] = {0, 1, 2, 3};

	fill_fifo(BIT(1));

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));

	check_fifo_xyz(chan_accel, all, FIFO_FRAMES);
	check_fifo_xyz(chan_gyro, all, FIFO_FRAMES);
	check_fifo_temp(all, FIFO_FRAMES);

	/* Single axes are not decoded from FIFO frames */
	for (int i = 0; i < ARRAY_SIZE(chan_axes); i++) {
		check_unsupported(chan_axes[i]);
	}
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_LIGHT, 0});
}

ZTEST(icm566xx_decoder, test_fifo_full)
{
	static const int all[] = {0, 1, 2, 3};

	/* Data ready may be flagged along with FIFO events */
	fill_fifo(BIT(2) | BIT(0));

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	check_fifo_xyz(chan_accel, all, FIFO_FRAMES);
	check_fifo_temp(all, FIFO_FRAMES);
}

ZTEST(icm566xx_decoder, test_fifo_no_data)
{
	static const int accel_frames[] = {0, 2, 3};
	static const int gyro_frames[] = {0, 1, 2};
	static const int temp_frames[] = {1, 2, 3};

	fill_fifo(BIT(1));
	fifo_no_data(1, 5);
	fifo_no_data(3, 9);
	fifo_no_data(0, 13);

	check_fifo_xyz(chan_accel, accel_frames, ARRAY_SIZE(accel_frames));
	check_fifo_xyz(chan_gyro, gyro_frames, ARRAY_SIZE(gyro_frames));
	check_fifo_temp(temp_frames, ARRAY_SIZE(temp_frames));
}

ZTEST(icm566xx_decoder, test_fifo_bad_header)
{
	fill_fifo(BIT(1));
	/* Extended header */
	edata->fifo_payload[2].buf[0] = 0xF8;

	check_unsupported(chan_accel);
	check_unsupported(chan_temp);
}

ZTEST(icm566xx_decoder, test_header_only)
{
	/* Stream buffer for a FIFO trigger with SENSOR_STREAM_DATA_NOP or _DROP */
	memset(buffer, 0, sizeof(buffer));
	edata->header.timestamp = TIMESTAMP_NS;
	edata->header.events = BIT(1);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	check_no_data(chan_accel);
	check_no_data(chan_gyro);
	check_no_data(chan_temp);

	/* Same for a data ready trigger */
	edata->header.events = BIT(0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	check_no_data(chan_accel);
	check_no_data(chan_axes[4]);
}

ZTEST_SUITE(icm566xx_decoder, NULL, NULL, NULL, NULL, NULL);
