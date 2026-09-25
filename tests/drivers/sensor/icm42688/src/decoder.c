/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "icm4268x_decoder.h"
#include "icm4268x_emul.h"
#include "icm4268x_reg.h"

#define LAST_TS_NS 5000000000ULL

/* pi in nano-units */
#define PI_NANO 3141592654LL

#define FIFO_HDR_16                                                                                \
	(FIFO_HEADER_ACCEL | FIFO_HEADER_GYRO | FIELD_PREP(FIFO_HEADER_TIMESTAMP_FSYNC, 2))
#define FIFO_HDR_20 (FIFO_HDR_16 | FIFO_HEADER_20)

static const struct sensor_decoder_api *decoder;

static const struct sensor_chan_spec chan_accel = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec chan_gyro = {SENSOR_CHAN_GYRO_XYZ, 0};
static const struct sensor_chan_spec chan_temp = {SENSOR_CHAN_DIE_TEMP, 0};

static const struct alignment identity[3] = {{0, 1}, {1, 1}, {2, 1}};
/* x <- -z, y <- x, z <- -y */
static const struct alignment remap[3] = {{2, -1}, {0, 1}, {1, -1}};

static int64_t q31_to_nano(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000000) >> (31 - shift);
}

/*
 * Check a decoded value against the expected one, in nano-units. The allowed error is two
 * q31 LSBs at the shift, for the truncation of the conversion, plus 1e-6 of the value, for
 * the approximation of pi, plus two nano-units for the truncations of the check itself.
 */
static void check_value(q31_t value, int8_t shift, int64_t expected)
{
	int64_t actual = q31_to_nano(value, shift);
	int64_t tolerance = (INT64_C(2000000000) >> (31 - shift)) +
			    ((expected < 0) ? -expected : expected) / 1000000 + 2;

	zassert_within(actual, expected, tolerance, "got %" PRId64 ", expected %" PRId64, actual,
		       expected);
}

/* Accelerometer, in nm/s^2 */
static int64_t accel_nano(int32_t raw, int64_t lsb_per_g)
{
	return (int64_t)raw * SENSOR_G * 1000 / lsb_per_g;
}

/* Gyroscope, in nrad/s, with the sensitivity in LSB per 1000 dps */
static int64_t gyro_nano(int32_t raw, int64_t lsb_per_kdps)
{
	return (int64_t)raw * PI_NANO * 1000 / (180 * lsb_per_kdps);
}

/* Temperature, in nano-C, with the sensitivity in hundredths of LSB/C */
static int64_t temp_nano(int32_t raw, int64_t lsb_per_100c)
{
	return (int64_t)raw * 100000000000 / lsb_per_100c + 25000000000;
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void check_frame_count(const uint8_t *buffer, struct sensor_chan_spec chan,
			      uint16_t expected)
{
	uint16_t frame_count;

	zassert_ok(decoder->get_frame_count(buffer, chan, &frame_count));
	zassert_equal(frame_count, expected, "chan %d: %u frames, expected %u", chan.chan_type,
		      frame_count, expected);
}

ZTEST(icm4268x_decoder, test_size_info)
{
	static const enum sensor_channel q31_chans[] = {
		SENSOR_CHAN_ACCEL_X, SENSOR_CHAN_ACCEL_Y, SENSOR_CHAN_ACCEL_Z,  SENSOR_CHAN_GYRO_X,
		SENSOR_CHAN_GYRO_Y,  SENSOR_CHAN_GYRO_Z,  SENSOR_CHAN_DIE_TEMP,
	};
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_accel, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));
	zassert_ok(decoder->get_size_info(chan_gyro, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	ARRAY_FOR_EACH(q31_chans, i) {
		zassert_ok(decoder->get_size_info((struct sensor_chan_spec){q31_chans[i], 0},
						  &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}

	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

/* One-shot data */

static struct icm4268x_encoded_data one_shot;

static void fill_one_shot(enum icm4268x_variant variant, uint8_t accel_fs, uint8_t gyro_fs,
			  const struct alignment align[3], const int16_t raw[7])
{
	memset(&one_shot, 0, sizeof(one_shot));
	one_shot.header.timestamp = LAST_TS_NS;
	one_shot.header.is_fifo = 0;
	one_shot.header.variant = variant;
	one_shot.header.accel_fs = accel_fs;
	one_shot.header.gyro_fs = gyro_fs;
	memcpy(one_shot.header.axis_align, align, sizeof(one_shot.header.axis_align));
	one_shot.channels = BIT_MASK(7);

	for (int i = 0; i < 7; i++) {
		sys_put_be16((uint16_t)raw[i], &one_shot.readings[i * 2]);
	}
}

static void check_one_shot(struct sensor_chan_spec chan, int8_t shift, const int64_t *expected)
{
	const uint8_t *buffer = (const uint8_t *)&one_shot;
	struct sensor_three_axis_data out;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)&out;
	struct sensor_q31_sample_data *q31_readings = out_q31->readings;
	int num = SENSOR_CHANNEL_3_AXIS(chan.chan_type) ? 3 : 1;
	uint32_t fit = 0;

	check_frame_count(buffer, chan, 1);

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, LAST_TS_NS);
	zassert_equal(out.shift, shift, "chan %d: shift %d", chan.chan_type, out.shift);

	if (num == 3) {
		zassert_equal(out.readings[0].timestamp_delta, 0);
		for (int i = 0; i < 3; i++) {
			check_value(out.readings[0].values[i], out.shift, expected[i]);
		}
	} else {
		zassert_equal(q31_readings[0].timestamp_delta, 0);
		check_value(q31_readings[0].value, out_q31->shift, expected[0]);
	}

	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

ZTEST(icm4268x_decoder, test_one_shot_icm42688)
{
	const int16_t raw[7] = {-1325, 16384, -8192, 4096, 1640, -3280, 32767};
	const int64_t accel[3] = {accel_nano(16384, 2048), accel_nano(-8192, 2048),
				  accel_nano(4096, 2048)};
	const int64_t gyro[3] = {gyro_nano(1640, 2097200), gyro_nano(-3280, 2097200),
				 gyro_nano(32767, 2097200)};
	const int64_t temp = temp_nano(-1325, 13248);
	const uint8_t *buffer = (const uint8_t *)&one_shot;

	fill_one_shot(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_16,
		      ICM42688_DT_GYRO_FS_15_625, identity, raw);

	check_one_shot(chan_accel, 8, accel);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0}, 8, &accel[0]);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0}, 8, &accel[1]);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, 8, &accel[2]);
	check_one_shot(chan_gyro, -1, gyro);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0}, -1, &gyro[0]);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0}, -1, &gyro[1]);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0}, -1, &gyro[2]);
	check_one_shot(chan_temp, 9, &temp);

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(icm4268x_decoder, test_one_shot_icm42686_remap)
{
	const int16_t raw[7] = {2650, -32768, 1024, 20000, -82, 8200, -16400};
	/* x <- -z, y <- x, z <- -y */
	const int64_t accel[3] = {accel_nano(-20000, 1024), accel_nano(-32768, 1024),
				  accel_nano(-1024, 1024)};
	const int64_t gyro[3] = {gyro_nano(16400, 8200), gyro_nano(-82, 8200),
				 gyro_nano(-8200, 8200)};
	const int64_t accel_2g[3] = {accel_nano(-20000, 16384), accel_nano(-32768, 16384),
				     accel_nano(-1024, 16384)};
	const int64_t gyro_500dps[3] = {gyro_nano(16400, 65500), gyro_nano(-82, 65500),
					gyro_nano(-8200, 65500)};

	fill_one_shot(ICM4268X_VARIANT_ICM42686, ICM42686_DT_ACCEL_FS_32, ICM42686_DT_GYRO_FS_4000,
		      remap, raw);

	check_one_shot(chan_accel, 9, accel);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0}, 9, &accel[0]);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, 9, &accel[2]);
	check_one_shot(chan_gyro, 7, gyro);
	check_one_shot((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0}, 7, &gyro[1]);

	fill_one_shot(ICM4268X_VARIANT_ICM42686, ICM42686_DT_ACCEL_FS_2, ICM42686_DT_GYRO_FS_500,
		      remap, raw);

	check_one_shot(chan_accel, 5, accel_2g);
	check_one_shot(chan_gyro, 4, gyro_500dps);
}

ZTEST(icm4268x_decoder, test_one_shot_missing_channel)
{
	const int16_t raw[7] = {0};
	const uint8_t *buffer = (const uint8_t *)&one_shot;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_one_shot(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_2, ICM42688_DT_GYRO_FS_2000,
		      identity, raw);
	/* Only DIE_TEMP and ACCEL_X were requested */
	one_shot.channels = BIT(0) | BIT(1);

	check_frame_count(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0}, 1);
	zassert_equal(decoder->get_frame_count(buffer, chan_accel, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->get_frame_count(buffer, chan_gyro, &frame_count), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 0, &out), -ENODATA);

	/* No reading requested */
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 0, &out), 0);
	zassert_equal(fit, 0);
}

/* FIFO data */

#define FIFO_MAX_SIZE 256

static struct {
	struct icm4268x_fifo_data hdr;
	uint8_t data[FIFO_MAX_SIZE];
} __aligned(8) fifo;

static void fill_fifo_hdr(enum icm4268x_variant variant, uint8_t accel_fs, uint8_t gyro_fs,
			  uint8_t accel_odr, uint8_t gyro_odr, uint16_t fifo_count)
{
	memset(&fifo.hdr, 0, sizeof(fifo.hdr));
	fifo.hdr.header.timestamp = LAST_TS_NS;
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.variant = variant;
	fifo.hdr.header.accel_fs = accel_fs;
	fifo.hdr.header.gyro_fs = gyro_fs;
	memcpy(fifo.hdr.header.axis_align, identity, sizeof(fifo.hdr.header.axis_align));
	fifo.hdr.int_status = BIT_FIFO_THS_INT;
	fifo.hdr.accel_odr = accel_odr;
	fifo.hdr.gyro_odr = gyro_odr;
	fifo.hdr.fifo_count = fifo_count;
	fifo.hdr.rtc_freq = 32000;
}

static size_t put_packet_16(size_t pos, const int16_t accel[3], const int16_t gyro[3], int8_t temp)
{
	uint8_t *pkt = &fifo.data[pos];

	pkt[0] = FIFO_HDR_16;
	for (int i = 0; i < 3; i++) {
		sys_put_be16((uint16_t)accel[i], &pkt[1 + i * 2]);
		sys_put_be16((uint16_t)gyro[i], &pkt[7 + i * 2]);
	}
	pkt[13] = (uint8_t)temp;
	sys_put_be16(0x1234, &pkt[14]);

	return pos + 16;
}

static size_t put_packet_8(size_t pos, bool is_accel, const int16_t data[3], int8_t temp)
{
	uint8_t *pkt = &fifo.data[pos];

	pkt[0] = is_accel ? FIFO_HEADER_ACCEL : FIFO_HEADER_GYRO;
	for (int i = 0; i < 3; i++) {
		sys_put_be16((uint16_t)data[i], &pkt[1 + i * 2]);
	}
	pkt[7] = (uint8_t)temp;

	return pos + 8;
}

static size_t put_packet_20(size_t pos, const int32_t accel[3], const int32_t gyro[3], int16_t temp)
{
	uint8_t *pkt = &fifo.data[pos];

	pkt[0] = FIFO_HDR_20;
	for (int i = 0; i < 3; i++) {
		sys_put_be16((uint16_t)((uint32_t)accel[i] >> 4), &pkt[1 + i * 2]);
		sys_put_be16((uint16_t)((uint32_t)gyro[i] >> 4), &pkt[7 + i * 2]);
		pkt[17 + i] = (uint8_t)(((accel[i] & 0xf) << 4) | (gyro[i] & 0xf));
	}
	sys_put_be16((uint16_t)temp, &pkt[13]);
	sys_put_be16(0x1234, &pkt[15]);

	return pos + 20;
}

/* Check reading i of n of a FIFO channel decoded in out */
static void check_fifo_ts(const struct sensor_data_header *header, uint32_t delta, int i, int n,
			  uint64_t period_ns)
{
	uint64_t expected = LAST_TS_NS - (uint64_t)(n - 1 - i) * period_ns;

	zassert_equal(header->base_timestamp_ns + delta, expected,
		      "reading %d: %" PRIu64 ", expected %" PRIu64, i,
		      header->base_timestamp_ns + delta, expected);
}

#define NUM_PACKETS 4

static const int16_t fifo_accel[NUM_PACKETS][3] = {
	{100, -200, 2048},
	{-32768, 32767, 0},
	{1000, 2000, -3000},
	{-1, 1, 16384},
};
static const int16_t fifo_gyro[NUM_PACKETS][3] = {
	{164, -328, 16384},
	{32767, -32768, 1},
	{-5000, 5000, 0},
	{10, -10, 20000},
};
static const int8_t fifo_temp[NUM_PACKETS] = {10, -20, 127, -128};

/* Sensitivities of 16-bit FIFO data, used by check_fifo_16_reading() */
static int64_t fifo_accel_lsb_per_g;
static int64_t fifo_gyro_lsb_per_kdps;

static void set_fifo_16_sensitivity(int64_t accel_lsb_per_g, int64_t gyro_lsb_per_kdps)
{
	fifo_accel_lsb_per_g = accel_lsb_per_g;
	fifo_gyro_lsb_per_kdps = gyro_lsb_per_kdps;
}

/* ICM42688 at +/-4 g and +/-250 dps: 8192 LSB/g and 131.072 LSB/dps */
static void fill_fifo_16(uint8_t accel_odr, uint8_t gyro_odr)
{
	size_t pos = 0;

	for (int i = 0; i < NUM_PACKETS; i++) {
		pos = put_packet_16(pos, fifo_accel[i], fifo_gyro[i], fifo_temp[i]);
	}

	fill_fifo_hdr(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_4, ICM42688_DT_GYRO_FS_250,
		      accel_odr, gyro_odr, pos);
	set_fifo_16_sensitivity(8192, 131072);
}

static void check_fifo_16_reading(struct sensor_chan_spec chan, const q31_t *values, int8_t shift,
				  int i)
{
	switch (chan.chan_type) {
	case SENSOR_CHAN_ACCEL_XYZ:
		for (int axis = 0; axis < 3; axis++) {
			check_value(values[axis], shift,
				    accel_nano(fifo_accel[i][axis], fifo_accel_lsb_per_g));
		}
		break;
	case SENSOR_CHAN_ACCEL_X:
	case SENSOR_CHAN_ACCEL_Y:
	case SENSOR_CHAN_ACCEL_Z:
		check_value(values[0], shift,
			    accel_nano(fifo_accel[i][chan.chan_type - SENSOR_CHAN_ACCEL_X],
				       fifo_accel_lsb_per_g));
		break;
	case SENSOR_CHAN_GYRO_XYZ:
		for (int axis = 0; axis < 3; axis++) {
			check_value(values[axis], shift,
				    gyro_nano(fifo_gyro[i][axis], fifo_gyro_lsb_per_kdps));
		}
		break;
	case SENSOR_CHAN_GYRO_X:
	case SENSOR_CHAN_GYRO_Y:
	case SENSOR_CHAN_GYRO_Z:
		check_value(values[0], shift,
			    gyro_nano(fifo_gyro[i][chan.chan_type - SENSOR_CHAN_GYRO_X],
				      fifo_gyro_lsb_per_kdps));
		break;
	case SENSOR_CHAN_DIE_TEMP:
		check_value(values[0], shift, temp_nano(fifo_temp[i], 207));
		break;
	default:
		ztest_test_fail();
	}
}

static void check_fifo_16(struct sensor_chan_spec chan, int8_t shift, uint64_t period_ns)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const bool three_axis = SENSOR_CHANNEL_3_AXIS(chan.chan_type);
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[NUM_PACKETS - 1];
	} out;
	struct sensor_three_axis_sample_data *readings = out.data.readings;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)&out;
	struct sensor_q31_sample_data *q31_readings = out_q31->readings;
	uint32_t fit = 0;

	check_frame_count(buffer, chan, NUM_PACKETS);

	/* All readings in one call */
	zassert_equal(decoder->decode(buffer, chan, &fit, NUM_PACKETS, &out), NUM_PACKETS);
	zassert_equal(out.data.header.reading_count, NUM_PACKETS);
	zassert_equal(out.data.shift, shift);
	for (int i = 0; i < NUM_PACKETS; i++) {
		if (three_axis) {
			check_fifo_ts(&out.data.header, readings[i].timestamp_delta, i, NUM_PACKETS,
				      period_ns);
			check_fifo_16_reading(chan, readings[i].values, shift, i);
		} else {
			check_fifo_ts(&out_q31->header, q31_readings[i].timestamp_delta, i,
				      NUM_PACKETS, period_ns);
			check_fifo_16_reading(chan, &q31_readings[i].value, shift, i);
		}
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, NUM_PACKETS, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < NUM_PACKETS; i++) {
		zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 1);
		zassert_equal(out.data.header.reading_count, 1);
		if (three_axis) {
			zassert_equal(readings[0].timestamp_delta, 0);
			check_fifo_ts(&out.data.header, readings[0].timestamp_delta, i, NUM_PACKETS,
				      period_ns);
			check_fifo_16_reading(chan, readings[0].values, shift, i);
		} else {
			zassert_equal(q31_readings[0].timestamp_delta, 0);
			check_fifo_ts(&out_q31->header, q31_readings[0].timestamp_delta, i,
				      NUM_PACKETS, period_ns);
			check_fifo_16_reading(chan, &q31_readings[0].value, shift, i);
		}
	}
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), 0);
}

ZTEST(icm4268x_decoder, test_fifo_16)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;

	fill_fifo_16(ICM4268X_DT_ACCEL_ODR_1000, ICM4268X_DT_GYRO_ODR_500);

	check_fifo_16(chan_accel, 6, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0}, 6, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0}, 6, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, 6, 1000000);
	check_fifo_16(chan_gyro, 3, 2000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0}, 3, 2000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0}, 3, 2000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0}, 3, 2000000);
	/* The temperature follows the accelerometer */
	check_fifo_16(chan_temp, 9, 1000000);

	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	fifo.hdr.int_status = BIT_FIFO_FULL_INT | BIT_DATA_RDY_INT;
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_MOTION));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(icm4268x_decoder, test_fifo_16_full_scale)
{
	fill_fifo_16(ICM4268X_DT_ACCEL_ODR_1000, ICM4268X_DT_GYRO_ODR_1000);

	/* +/-2 g and +/-15.625 dps: 16384 LSB/g and 2097.152 LSB/dps, the gyroscope shift is -1 */
	fifo.hdr.header.accel_fs = ICM42688_DT_ACCEL_FS_2;
	fifo.hdr.header.gyro_fs = ICM42688_DT_GYRO_FS_15_625;
	set_fifo_16_sensitivity(16384, 2097152);

	check_fifo_16(chan_accel, 5, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, 5, 1000000);
	check_fifo_16(chan_gyro, -1, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0}, -1, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0}, -1, 1000000);
	check_fifo_16((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0}, -1, 1000000);
}

ZTEST(icm4268x_decoder, test_fifo_rtc_freq)
{
	fill_fifo_16(ICM4268X_DT_ACCEL_ODR_1000, ICM4268X_DT_GYRO_ODR_32000);

	/* An external clock at 40 kHz speeds up the ODR by 40000 / 32000 */
	fifo.hdr.rtc_freq = 40000;
	check_fifo_16(chan_accel, 6, 800000);
	check_fifo_16(chan_gyro, 3, 25000);
}

ZTEST(icm4268x_decoder, test_fifo_slow_odr)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[7];
	} out;
	struct sensor_three_axis_sample_data *readings = out.data.readings;
	const uint64_t period_ns = 640000000;
	size_t pos = 0;
	uint32_t fit = 0;
	int count;

	for (int i = 0; i < 8; i++) {
		pos = put_packet_16(pos, fifo_accel[i % NUM_PACKETS], fifo_gyro[i % NUM_PACKETS],
				    fifo_temp[i % NUM_PACKETS]);
	}
	fill_fifo_hdr(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_4, ICM42688_DT_GYRO_FS_250,
		      ICM4268X_DT_ACCEL_ODR_1_5625, ICM4268X_DT_GYRO_ODR_12_5, pos);
	set_fifo_16_sensitivity(8192, 131072);

	check_frame_count(buffer, chan_accel, 8);

	/* 7 periods of 640 ms do not fit in a 32-bit delta: the batch is split */
	count = decoder->decode(buffer, chan_accel, &fit, 8, &out);
	zassert_equal(count, 7);
	for (int i = 0; i < count; i++) {
		check_fifo_ts(&out.data.header, readings[i].timestamp_delta, i, 8, period_ns);
	}
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 8, &out), 1);
	check_fifo_ts(&out.data.header, readings[0].timestamp_delta, 7, 8, period_ns);
	check_fifo_16_reading(chan_accel, readings[0].values, out.data.shift, 3);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 8, &out), 0);
}

ZTEST(icm4268x_decoder, test_fifo_mixed_packets)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[3];
	} out;
	struct sensor_three_axis_sample_data *readings = out.data.readings;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)&out;
	struct sensor_q31_sample_data *q31_readings = out_q31->readings;
	size_t pos = 0;
	uint32_t fit = 0;

	/* accel, gyro, accel + gyro, accel, then a partial packet */
	pos = put_packet_8(pos, true, fifo_accel[0], fifo_temp[0]);
	pos = put_packet_8(pos, false, fifo_gyro[1], fifo_temp[1]);
	pos = put_packet_16(pos, fifo_accel[2], fifo_gyro[2], fifo_temp[2]);
	pos = put_packet_8(pos, true, fifo_accel[3], fifo_temp[3]);
	fifo.data[pos] = FIFO_HDR_16;
	pos += 10;
	fill_fifo_hdr(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_4, ICM42688_DT_GYRO_FS_250,
		      ICM4268X_DT_ACCEL_ODR_100, ICM4268X_DT_GYRO_ODR_200, pos);
	set_fifo_16_sensitivity(8192, 131072);

	check_frame_count(buffer, chan_accel, 3);
	check_frame_count(buffer, chan_gyro, 2);
	check_frame_count(buffer, chan_temp, 4);

	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 4, &out), 3);
	for (int i = 0; i < 3; i++) {
		check_fifo_ts(&out.data.header, readings[i].timestamp_delta, i, 3, 10000000);
	}
	check_fifo_16_reading(chan_accel, readings[0].values, out.data.shift, 0);
	check_fifo_16_reading(chan_accel, readings[1].values, out.data.shift, 2);
	check_fifo_16_reading(chan_accel, readings[2].values, out.data.shift, 3);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 4, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 4, &out), 2);
	for (int i = 0; i < 2; i++) {
		check_fifo_ts(&out.data.header, readings[i].timestamp_delta, i, 2, 5000000);
	}
	check_fifo_16_reading(chan_gyro, readings[0].values, out.data.shift, 1);
	check_fifo_16_reading(chan_gyro, readings[1].values, out.data.shift, 2);

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 4, &out), 4);
	for (int i = 0; i < 4; i++) {
		check_fifo_ts(&out_q31->header, q31_readings[i].timestamp_delta, i, 4, 10000000);
		check_fifo_16_reading(chan_temp, &q31_readings[i].value, out_q31->shift, i);
	}
}

ZTEST(icm4268x_decoder, test_fifo_missing_sensor)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[1];
	} out;
	uint32_t fit = 0;
	size_t pos = 0;

	/* Gyroscope packets only */
	pos = put_packet_8(pos, false, fifo_gyro[0], fifo_temp[0]);
	pos = put_packet_8(pos, false, fifo_gyro[1], fifo_temp[1]);
	fill_fifo_hdr(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_4, ICM42688_DT_GYRO_FS_250,
		      ICM4268X_DT_ACCEL_ODR_100, ICM4268X_DT_GYRO_ODR_200, pos);

	check_frame_count(buffer, chan_accel, 0);
	check_frame_count(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0}, 0);
	check_frame_count(buffer, chan_gyro, 2);
	check_frame_count(buffer, chan_temp, 2);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 0, &out), -ENODATA);
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0},
				      &fit, 1, &out),
		      -ENODATA);
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 2, &out), 2);
}

ZTEST(icm4268x_decoder, test_fifo_20)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	const int32_t accel[3][3] = {
		{-524287, 524287, 1},
		{-524288, 0, 0},
		{123456, -654321 / 2, 16},
	};
	const int32_t gyro[3][3] = {
		{262144, -262144, -1},
		{1000, 2000, 3000},
		{-524288, 0, 0},
	};
	const int16_t temp[3] = {-2650, 0, 13248};
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[2];
	} out;
	struct sensor_three_axis_sample_data *readings = out.data.readings;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)&out;
	struct sensor_q31_sample_data *q31_readings = out_q31->readings;
	size_t pos = 0;
	uint32_t fit = 0;

	for (int i = 0; i < 3; i++) {
		pos = put_packet_20(pos, accel[i], gyro[i], temp[i]);
	}

	/* High resolution data is at the highest full scale */
	fill_fifo_hdr(ICM4268X_VARIANT_ICM42686, ICM42686_DT_ACCEL_FS_32, ICM42686_DT_GYRO_FS_4000,
		      ICM4268X_DT_ACCEL_ODR_200, ICM4268X_DT_GYRO_ODR_100, pos);
	memcpy(fifo.hdr.header.axis_align, remap, sizeof(fifo.hdr.header.axis_align));

	check_frame_count(buffer, chan_accel, 3);
	check_frame_count(buffer, chan_gyro, 3);
	check_frame_count(buffer, chan_temp, 3);

	/* The second accelerometer sample is invalid but keeps its time slot */
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 3, &out), 2);
	zassert_equal(out.data.shift, 9);
	check_fifo_ts(&out.data.header, readings[0].timestamp_delta, 0, 3, 5000000);
	check_fifo_ts(&out.data.header, readings[1].timestamp_delta, 2, 3, 5000000);
	/* x <- -z, y <- x, z <- -y; 32 g over 2^19 LSB */
	check_value(readings[0].x, 9, accel_nano(-1, 16384));
	check_value(readings[0].y, 9, accel_nano(-524287, 16384));
	check_value(readings[0].z, 9, accel_nano(-524287, 16384));
	check_value(readings[1].x, 9, accel_nano(-16, 16384));
	check_value(readings[1].y, 9, accel_nano(123456, 16384));
	check_value(readings[1].z, 9, accel_nano(654321 / 2, 16384));
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 3, &out), 0);

	/* One reading per call skips the invalid sample too */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 1, &out), 1);
	zassert_equal(out.data.shift, 7);
	check_fifo_ts(&out.data.header, readings[0].timestamp_delta, 0, 3, 10000000);
	/* 4000 dps over 2^19 LSB: 131.072 LSB/dps */
	check_value(readings[0].x, 7, gyro_nano(1, 131072));
	check_value(readings[0].y, 7, gyro_nano(262144, 131072));
	check_value(readings[0].z, 7, gyro_nano(262144, 131072));
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 1, &out), 1);
	check_fifo_ts(&out.data.header, readings[0].timestamp_delta, 1, 3, 10000000);
	check_value(readings[0].x, 7, gyro_nano(-3000, 131072));
	check_value(readings[0].y, 7, gyro_nano(1000, 131072));
	check_value(readings[0].z, 7, gyro_nano(-2000, 131072));
	zassert_equal(decoder->decode(buffer, chan_gyro, &fit, 1, &out), 0);

	/* Single axes are checked on their own: -z is valid in the second packet */
	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0},
				      &fit, 3, &out),
		      3);
	check_value(q31_readings[1].value, 9, accel_nano(0, 16384));

	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_temp, &fit, 3, &out), 3);
	for (int i = 0; i < 3; i++) {
		check_fifo_ts(&out_q31->header, q31_readings[i].timestamp_delta, i, 3, 5000000);
		check_value(q31_readings[i].value, 9, temp_nano(temp[i], 13248));
	}
}

ZTEST(icm4268x_decoder, test_fifo_empty)
{
	const uint8_t *buffer = (const uint8_t *)&fifo;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* Buffer completed without data */
	fill_fifo_hdr(ICM4268X_VARIANT_ICM42688, ICM42688_DT_ACCEL_FS_16, ICM42688_DT_GYRO_FS_2000,
		      ICM4268X_DT_ACCEL_ODR_1000, ICM4268X_DT_GYRO_ODR_1000, 0);

	check_frame_count(buffer, chan_accel, 0);
	check_frame_count(buffer, chan_temp, 0);
	zassert_equal(decoder->decode(buffer, chan_accel, &fit, 1, &out), 0);
	zassert_true(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

/* One-shot read through the emulator */

SENSOR_DT_READ_IODEV(icm42688_iodev, DT_NODELABEL(icm42688), {SENSOR_CHAN_ACCEL_XYZ, 0},
		     {SENSOR_CHAN_GYRO_XYZ, 0}, {SENSOR_CHAN_DIE_TEMP, 0});
RTIO_DEFINE(icm42688_rtio, 1, 1);

ZTEST(icm4268x_decoder, test_read)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(icm42688));
	const struct emul *target = EMUL_DT_GET(DT_NODELABEL(icm42688));
	const int16_t raw[7] = {-2650, 8192, -4096, 12345, 655, -1310, 30000};
	uint8_t regs[14];
	uint8_t status = BIT_DATA_RDY_INT;
	uint8_t buf[sizeof(struct icm4268x_encoded_data)] __aligned(8);
	struct sensor_three_axis_data out;
	struct sensor_q31_data *out_q31 = (struct sensor_q31_data *)&out;
	struct sensor_q31_sample_data *q31_readings = out_q31->readings;
	struct sensor_value val;
	uint32_t fit = 0;

	sensor_g_to_ms2(5, &val);
	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &val));
	sensor_degrees_to_rad(501, &val);
	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE, &val));

	for (int i = 0; i < 7; i++) {
		sys_put_be16((uint16_t)raw[i], &regs[i * 2]);
	}
	icm4268x_emul_set_reg(target, REG_INT_STATUS, &status, 1);
	icm4268x_emul_set_reg(target, REG_TEMP_DATA1, regs, sizeof(regs));

	zassert_ok(sensor_read(&icm42688_iodev, &icm42688_rtio, buf, sizeof(buf)));

	zassert_equal(decoder->decode(buf, chan_accel, &fit, 1, &out), 1);
	zassert_equal(out.shift, 6);
	check_value(out.readings[0].x, out.shift, accel_nano(8192, 8192));
	check_value(out.readings[0].y, out.shift, accel_nano(-4096, 8192));
	check_value(out.readings[0].z, out.shift, accel_nano(12345, 8192));

	fit = 0;
	zassert_equal(decoder->decode(buf, chan_gyro, &fit, 1, &out), 1);
	zassert_equal(out.shift, 4);
	check_value(out.readings[0].x, out.shift, gyro_nano(655, 65500));
	check_value(out.readings[0].y, out.shift, gyro_nano(-1310, 65500));
	check_value(out.readings[0].z, out.shift, gyro_nano(30000, 65500));

	fit = 0;
	zassert_equal(decoder->decode(buf, chan_temp, &fit, 1, &out), 1);
	check_value(q31_readings[0].value, out_q31->shift, temp_nano(-2650, 13248));
}

static void *icm4268x_decoder_setup(void)
{
	zassert_ok(sensor_get_decoder(DEVICE_DT_GET(DT_NODELABEL(icm42688)), &decoder));

	return NULL;
}

ZTEST_SUITE(icm4268x_decoder, NULL, icm4268x_decoder_setup, NULL, NULL, NULL);
