/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_decoder.h>
#include <zephyr/dt-bindings/sensor/lsm6dsv16x.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "lsm6dsv16x.h"
#include "lsm6dsv16x_decoder.h"

#define LAST_TS_NS   5000000000ULL
#define MAX_READINGS 16

static const struct sensor_decoder_api *decoder;

/* Output buffer for MAX_READINGS readings of any channel */
static union {
	struct sensor_q31_data q31;
	struct sensor_three_axis_data three;
	struct sensor_game_rotation_vector_data rot;
	uint8_t raw[sizeof(struct sensor_game_rotation_vector_data) +
		    (MAX_READINGS - 1) * sizeof(struct sensor_game_rotation_vector_sample_data)];
} out;

/* Expected reading, values in micro-units of the channel */
struct expected {
	uint64_t ts_ns;
	int64_t micro[4];
};

static struct sensor_chan_spec chan(enum sensor_channel type)
{
	return (struct sensor_chan_spec){type, 0};
}

static int64_t accel_micro(int16_t raw, uint8_t fs_idx)
{
	return (int64_t)raw * (GAIN_UNIT_XL << fs_idx) * SENSOR_G / 1000000LL;
}

static int64_t gyro_micro(int16_t raw, uint8_t lsb_log2)
{
	return (int64_t)raw * (GAIN_UNIT_G << lsb_log2) * SENSOR_PI / 180LL / 1000000LL;
}

static int64_t temp_micro(int16_t raw)
{
	return 25000000LL + (int64_t)raw * 1000000LL / 256LL;
}

static uint8_t num_values(enum sensor_channel type)
{
	switch (type) {
	case SENSOR_CHAN_GAME_ROTATION_VECTOR:
		return 4;
	case SENSOR_CHAN_ACCEL_XYZ:
	case SENSOR_CHAN_GYRO_XYZ:
	case SENSOR_CHAN_MAGN_XYZ:
	case SENSOR_CHAN_GRAVITY_VECTOR:
	case SENSOR_CHAN_GBIAS_XYZ:
		return 3;
	default:
		return 1;
	}
}

static q31_t out_value(uint8_t nv, int i, int j)
{
	switch (nv) {
	case 4:
		return out.rot.readings[i].values[j];
	case 3:
		return out.three.readings[i].values[j];
	default:
		return out.q31.readings[i].value;
	}
}

static uint32_t out_delta(uint8_t nv, int i)
{
	switch (nv) {
	case 4:
		return out.rot.readings[i].timestamp_delta;
	case 3:
		return out.three.readings[i].timestamp_delta;
	default:
		return out.q31.readings[i].timestamp_delta;
	}
}

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000LL) >> (31 - shift);
}

static void check_reading(struct sensor_chan_spec ch, int i, int8_t shift,
			  const struct expected *exp)
{
	const uint8_t nv = num_values(ch.chan_type);

	zassert_equal(out.q31.header.base_timestamp_ns + out_delta(nv, i), exp->ts_ns,
		      "chan %d reading %d: timestamp %llu, expected %llu", ch.chan_type, i,
		      out.q31.header.base_timestamp_ns + out_delta(nv, i), exp->ts_ns);

	for (int j = 0; j < nv; j++) {
		int64_t actual = q31_to_micro(out_value(nv, i, j), shift);
		int64_t tolerance = llabs(exp->micro[j]) / 2000 + 10;

		zassert_within(actual, exp->micro[j], tolerance,
			       "chan %d value %d: got %lld, expected %lld", ch.chan_type, j, actual,
			       exp->micro[j]);
	}
}

/*
 * Check the readings of a channel decoded as many at a time as possible, then one by one. A
 * batch holds all readings unless they span more than UINT32_MAX ns.
 */
static void check_readings(const uint8_t *buffer, struct sensor_chan_spec ch, int8_t shift,
			   const struct expected *exp, int n)
{
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;
	int total = 0;
	int rc;

	zassert_ok(decoder->get_frame_count(buffer, ch, &frame_count));
	zassert_equal(frame_count, n, "chan %d: %u frames, expected %d", ch.chan_type, frame_count,
		      n);

	zassert_ok(decoder->get_size_info(ch, &base_size, &frame_size));
	zassert_true(base_size + (MAX_READINGS - 1) * frame_size <= sizeof(out));

	memset(&out, 0xa5, sizeof(out));
	while ((rc = decoder->decode(buffer, ch, &fit, MAX_READINGS, &out)) > 0) {
		zassert_true(total + rc <= n);
		zassert_equal(out.q31.header.reading_count, rc);
		zassert_equal(out.q31.shift, shift);
		for (int i = 0; i < rc; i++) {
			check_reading(ch, i, shift, &exp[total + i]);
		}
		total += rc;
		memset(&out, 0xa5, sizeof(out));
	}
	zassert_equal(rc, 0, "chan %d: decode returned %d", ch.chan_type, rc);
	zassert_equal(total, n, "chan %d: %d readings decoded, expected %d", ch.chan_type, total,
		      n);
	if (n <= MAX_READINGS && exp[n - 1].ts_ns - exp[0].ts_ns <= UINT32_MAX) {
		/* All readings fit in one batch */
		fit = 0;
		zassert_equal(decoder->decode(buffer, ch, &fit, MAX_READINGS, &out), n);
	}

	fit = 0;
	for (int i = 0; i < n; i++) {
		memset(&out, 0xa5, sizeof(out));
		zassert_equal(decoder->decode(buffer, ch, &fit, 1, &out), 1);
		zassert_equal(out.q31.header.reading_count, 1);
		zassert_equal(out.q31.shift, shift);
		check_reading(ch, 0, shift, &exp[i]);
	}
	zassert_equal(decoder->decode(buffer, ch, &fit, 1, &out), 0);
}

/*
 * Check a supported channel without readings: decode() returns rc, which is -ENODATA when the
 * buffer holds data of other channels and 0 when it holds no data.
 */
static void check_no_readings(const uint8_t *buffer, struct sensor_chan_spec ch, int rc)
{
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(buffer, ch, &frame_count));
	zassert_equal(frame_count, 0, "chan %d: %u frames", ch.chan_type, frame_count);
	zassert_equal(decoder->decode(buffer, ch, &fit, MAX_READINGS, &out), rc,
		      "chan %d: decode did not return %d", ch.chan_type, rc);
	zassert_equal(fit, 0);
}

static void check_size_info(enum sensor_channel type, size_t base, size_t frame)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan(type), &base_size, &frame_size));
	zassert_equal(base_size, base, "chan %d", type);
	zassert_equal(frame_size, frame, "chan %d", type);
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec ch)
{
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_frame_count(buffer, ch, &frame_count), -ENOTSUP, "chan %d",
		      ch.chan_type);
	zassert_equal(decoder->decode(buffer, ch, &fit, 1, &out), -ENOTSUP, "chan %d",
		      ch.chan_type);
}

static void check_no_trigger(const uint8_t *buffer)
{
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_FULL));
}

ZTEST(lsm6dsv16x_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	check_size_info(SENSOR_CHAN_ACCEL_XYZ, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));
	check_size_info(SENSOR_CHAN_GYRO_XYZ, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));
	check_size_info(SENSOR_CHAN_ACCEL_X, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(SENSOR_CHAN_GYRO_Z, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));
	check_size_info(SENSOR_CHAN_DIE_TEMP, sizeof(struct sensor_q31_data),
			sizeof(struct sensor_q31_sample_data));

	check_size_info(SENSOR_CHAN_GAME_ROTATION_VECTOR,
			sizeof(struct sensor_game_rotation_vector_data),
			sizeof(struct sensor_game_rotation_vector_sample_data));
	check_size_info(SENSOR_CHAN_GRAVITY_VECTOR, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));
	check_size_info(SENSOR_CHAN_GBIAS_XYZ, sizeof(struct sensor_three_axis_data),
			sizeof(struct sensor_three_axis_sample_data));

	if (IS_ENABLED(CONFIG_LSM6DSV16X_SENSORHUB)) {
		check_size_info(SENSOR_CHAN_MAGN_XYZ, sizeof(struct sensor_three_axis_data),
				sizeof(struct sensor_three_axis_sample_data));
		check_size_info(SENSOR_CHAN_PRESS, sizeof(struct sensor_q31_data),
				sizeof(struct sensor_q31_sample_data));
	} else {
		zassert_equal(
			decoder->get_size_info(chan(SENSOR_CHAN_MAGN_XYZ), &base_size, &frame_size),
			-ENOTSUP);
	}

	zassert_equal(decoder->get_size_info(chan(SENSOR_CHAN_LIGHT), &base_size, &frame_size),
		      -ENOTSUP);
	zassert_equal(decoder->get_size_info((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1},
					     &base_size, &frame_size),
		      -ENOTSUP);
}

#define SAMPLE_TS_NS  123456789000ULL
/* +/-4 g and +/-2000 dps (16 times the 125 dps LSB) */
#define SAMPLE_FS_IDX LSM6DSV16X_ACCEL_FS_VAL_TO_FS_IDX(LSM6DSV16X_DT_FS_4G)
#define SAMPLE_GY_LOG 4

static struct lsm6dsv16x_rtio_data sample;

static void sample_init(void)
{
	memset(&sample, 0, sizeof(sample));
	sample.header.timestamp = SAMPLE_TS_NS;
	sample.header.accel_fs_idx = SAMPLE_FS_IDX;
	sample.header.gyro_fs = LSM6DSV16X_DT_FS_2000DPS;
	sample.has_accel = 1;
	sample.has_gyro = 1;
	sample.has_temp = 1;
	sample.acc[0] = 1000;
	sample.acc[1] = -2000;
	sample.acc[2] = 16000;
	sample.gyro[0] = INT16_MIN;
	sample.gyro[1] = 100;
	sample.gyro[2] = INT16_MAX;
	/* 20 degC */
	sample.temp = -1280;
}

ZTEST(lsm6dsv16x_decoder, test_single_sample)
{
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct expected exp = {.ts_ns = SAMPLE_TS_NS};

	sample_init();

	for (int i = 0; i < 3; i++) {
		exp.micro[i] = accel_micro(sample.acc[i], SAMPLE_FS_IDX);
	}
	check_readings(buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 6, &exp, 1);

	for (int i = 0; i < 3; i++) {
		exp.micro[0] = accel_micro(sample.acc[i], SAMPLE_FS_IDX);
		check_readings(buffer, chan(SENSOR_CHAN_ACCEL_X + i), 6, &exp, 1);
	}

	for (int i = 0; i < 3; i++) {
		exp.micro[i] = gyro_micro(sample.gyro[i], SAMPLE_GY_LOG);
	}
	check_readings(buffer, chan(SENSOR_CHAN_GYRO_XYZ), 6, &exp, 1);

	for (int i = 0; i < 3; i++) {
		exp.micro[0] = gyro_micro(sample.gyro[i], SAMPLE_GY_LOG);
		check_readings(buffer, chan(SENSOR_CHAN_GYRO_X + i), 6, &exp, 1);
	}

	exp.micro[0] = temp_micro(sample.temp);
	check_readings(buffer, chan(SENSOR_CHAN_DIE_TEMP), 9, &exp, 1);

	check_no_trigger(buffer);

	check_unsupported(buffer, chan(SENSOR_CHAN_GAME_ROTATION_VECTOR));
	check_unsupported(buffer, chan(SENSOR_CHAN_GRAVITY_VECTOR));
	check_unsupported(buffer, chan(SENSOR_CHAN_GBIAS_XYZ));
	check_unsupported(buffer, chan(SENSOR_CHAN_MAGN_XYZ));
	check_unsupported(buffer, chan(SENSOR_CHAN_PRESS));
	check_unsupported(buffer, chan(SENSOR_CHAN_LIGHT));
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
}

ZTEST(lsm6dsv16x_decoder, test_single_sample_missing)
{
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct expected exp = {.ts_ns = SAMPLE_TS_NS};

	/* Only the accelerometer was read, as in the data-ready stream */
	sample_init();
	sample.has_gyro = 0;
	sample.has_temp = 0;

	check_no_readings(buffer, chan(SENSOR_CHAN_GYRO_XYZ), -ENODATA);
	check_no_readings(buffer, chan(SENSOR_CHAN_GYRO_Y), -ENODATA);
	check_no_readings(buffer, chan(SENSOR_CHAN_DIE_TEMP), -ENODATA);

	exp.micro[0] = accel_micro(sample.acc[2], SAMPLE_FS_IDX);
	check_readings(buffer, chan(SENSOR_CHAN_ACCEL_Z), 6, &exp, 1);
}

ZTEST(lsm6dsv16x_decoder, test_single_sample_ranges)
{
	static const struct {
		uint8_t fs;
		int8_t shift;
		uint8_t lsb_log2;
	} gyro_fs[] = {
		{LSM6DSV16X_DT_FS_125DPS, 2, 0},  {LSM6DSV16X_DT_FS_250DPS, 3, 1},
		{LSM6DSV16X_DT_FS_500DPS, 4, 2},  {LSM6DSV16X_DT_FS_1000DPS, 5, 3},
		{LSM6DSV16X_DT_FS_2000DPS, 6, 4}, {LSM6DSV16X_DT_FS_4000DPS, 7, 5},
	};
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct expected exp = {.ts_ns = SAMPLE_TS_NS};

	sample_init();
	sample.acc[0] = INT16_MAX;
	sample.acc[1] = INT16_MIN;
	sample.acc[2] = -1;
	sample.gyro[0] = INT16_MAX;
	sample.gyro[1] = INT16_MIN;
	sample.gyro[2] = 1;

	/* +/-2 g to +/-16 g, and +/-32 g of the LSM6DSV32X */
	for (uint8_t idx = 0; idx <= 4; idx++) {
		sample.header.accel_fs_idx = idx;
		for (int i = 0; i < 3; i++) {
			exp.micro[i] = accel_micro(sample.acc[i], idx);
		}
		check_readings(buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 5 + idx, &exp, 1);
	}

	for (int k = 0; k < ARRAY_SIZE(gyro_fs); k++) {
		sample.header.gyro_fs = gyro_fs[k].fs;
		for (int i = 0; i < 3; i++) {
			exp.micro[i] = gyro_micro(sample.gyro[i], gyro_fs[k].lsb_log2);
		}
		check_readings(buffer, chan(SENSOR_CHAN_GYRO_XYZ), gyro_fs[k].shift, &exp, 1);
	}
}

ZTEST(lsm6dsv16x_decoder, test_single_sample_invalid_range)
{
	const uint8_t *buffer = (const uint8_t *)&sample;
	uint16_t frame_count;
	uint32_t fit = 0;

	sample_init();
	sample.header.accel_fs_idx = 5;
	sample.header.gyro_fs = 7;

	zassert_equal(decoder->get_frame_count(buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &frame_count),
		      -EINVAL);
	zassert_equal(decoder->decode(buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), -EINVAL);
	zassert_equal(decoder->decode(buffer, chan(SENSOR_CHAN_GYRO_X), &fit, 1, &out), -EINVAL);
	zassert_ok(decoder->get_frame_count(buffer, chan(SENSOR_CHAN_DIE_TEMP), &frame_count));
	zassert_equal(frame_count, 1);
}

#define MAX_WORDS 300

/* +/-8 g and +/-1000 dps (8 times the 125 dps LSB) */
#define FIFO_FS_IDX LSM6DSV16X_ACCEL_FS_VAL_TO_FS_IDX(LSM6DSV16X_DT_FS_8G)
#define FIFO_GY_LOG 3

#define XL_PERIOD_NS   SENSOR_ODR_MHZ_TO_PERIOD_NS(120000)
#define GY_PERIOD_NS   SENSOR_ODR_MHZ_TO_PERIOD_NS(240000)
#define TEMP_PERIOD_NS SENSOR_ODR_MHZ_TO_PERIOD_NS(15000)
#define SFLP_PERIOD_NS SENSOR_ODR_MHZ_TO_PERIOD_NS(60000)

static struct {
	struct lsm6dsv16x_fifo_data hdr;
	uint8_t words[MAX_WORDS * LSM6DSV16X_FIFO_ITEM_LEN];
} __packed fifo;

static const uint8_t *fifo_buffer = (const uint8_t *)&fifo;

static void fifo_init(void)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.timestamp = LAST_TS_NS;
	fifo.hdr.header.accel_fs_idx = FIFO_FS_IDX;
	fifo.hdr.header.gyro_fs = LSM6DSV16X_DT_FS_1000DPS;
	fifo.hdr.accel_batch_odr = LSM6DSVXXX_DT_XL_BATCHED_AT_120Hz;
	fifo.hdr.gyro_batch_odr = LSM6DSVXXX_DT_GY_BATCHED_AT_240Hz;
	fifo.hdr.temp_batch_odr = LSM6DSVXXX_DT_TEMP_BATCHED_AT_15Hz;
	fifo.hdr.sflp_batch_odr = LSM6DSVXXX_DT_SFLP_ODR_AT_60Hz;
}

static void put_word(uint8_t tag, uint16_t a, uint16_t b, uint16_t c)
{
	uint8_t *word = &fifo.words[fifo.hdr.fifo_count * LSM6DSV16X_FIFO_ITEM_LEN];

	zassert_true(fifo.hdr.fifo_count < MAX_WORDS);

	/* Word counter and parity bits next to the tag */
	word[0] = (tag << 3) | ((fifo.hdr.fifo_count & 0x3) << 1) | 1;
	sys_put_le16(a, &word[1]);
	sys_put_le16(b, &word[3]);
	sys_put_le16(c, &word[5]);
	fifo.hdr.fifo_count++;
}

static void put_word_3(uint8_t tag, const int16_t v[3])
{
	put_word(tag, (uint16_t)v[0], (uint16_t)v[1], (uint16_t)v[2]);
}

static uint64_t fifo_ts(int i, int n, uint64_t period_ns)
{
	return LAST_TS_NS - (uint64_t)(n - 1 - i) * period_ns;
}

ZTEST(lsm6dsv16x_decoder, test_fifo)
{
	static const int16_t xl[3][3] = {
		{100, -200, 300}, {-4000, 5000, 16384}, {INT16_MAX, INT16_MIN, 0}};
	static const int16_t gy[3][3] = {{10, 20, 30}, {-10, -20, -30}, {1000, -1000, INT16_MAX}};
	static const int16_t temp[2] = {1280, -512};
	static const int16_t gbias[2][3] = {{100, -100, 0}, {INT16_MAX, INT16_MIN, 1}};
	/* About 1 g */
	static const int16_t gravity[2][3] = {{0, 0, 16393}, {-16393, 0, 0}};
	/* Half-precision floats: 0.5, 0.25, -0.5 then 0.75, 0.75, 0 */
	static const uint16_t rot[2][3] = {{0x3800, 0x3400, 0xb800}, {0x3a00, 0x3a00, 0x0000}};
	static const int64_t rot_micro[2][4] = {{500000, 250000, -500000, 661438},
						{707107, 707107, 0, 0}};
	struct expected exp[3];

	fifo_init();
	put_word_3(LSM6DSV16X_XL_NC_TAG, xl[0]);
	put_word_3(LSM6DSV16X_GY_NC_TAG, gy[0]);
	put_word(LSM6DSV16X_TEMPERATURE_TAG, (uint16_t)temp[0], 0, 0);
	put_word(LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG, rot[0][0], rot[0][1], rot[0][2]);
	put_word_3(LSM6DSV16X_SFLP_GYROSCOPE_BIAS_TAG, gbias[0]);
	put_word_3(LSM6DSV16X_SFLP_GRAVITY_VECTOR_TAG, gravity[0]);
	/* Words of other kinds are skipped */
	put_word(LSM6DSV16X_TIMESTAMP_TAG, 0x1234, 0x5678, 0x9abc);
	put_word_3(LSM6DSV16X_XL_NC_TAG, xl[1]);
	put_word_3(LSM6DSV16X_GY_NC_TAG, gy[1]);
	put_word(LSM6DSV16X_CFG_CHANGE_TAG, 0, 0, 0);
	put_word_3(LSM6DSV16X_XL_NC_TAG, xl[2]);
	put_word(LSM6DSV16X_TEMPERATURE_TAG, (uint16_t)temp[1], 0, 0);
	put_word_3(LSM6DSV16X_GY_NC_TAG, gy[2]);
	put_word(LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG, rot[1][0], rot[1][1], rot[1][2]);
	put_word_3(LSM6DSV16X_SFLP_GYROSCOPE_BIAS_TAG, gbias[1]);
	put_word_3(LSM6DSV16X_SFLP_GRAVITY_VECTOR_TAG, gravity[1]);

	for (int i = 0; i < 3; i++) {
		exp[i].ts_ns = fifo_ts(i, 3, XL_PERIOD_NS);
		for (int j = 0; j < 3; j++) {
			exp[i].micro[j] = accel_micro(xl[i][j], FIFO_FS_IDX);
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 7, exp, 3);

	for (int i = 0; i < 3; i++) {
		exp[i].micro[0] = accel_micro(xl[i][1], FIFO_FS_IDX);
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_ACCEL_Y), 7, exp, 3);

	for (int i = 0; i < 3; i++) {
		exp[i].ts_ns = fifo_ts(i, 3, GY_PERIOD_NS);
		for (int j = 0; j < 3; j++) {
			exp[i].micro[j] = gyro_micro(gy[i][j], FIFO_GY_LOG);
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_GYRO_XYZ), 5, exp, 3);

	for (int i = 0; i < 3; i++) {
		exp[i].micro[0] = gyro_micro(gy[i][2], FIFO_GY_LOG);
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_GYRO_Z), 5, exp, 3);

	for (int i = 0; i < 2; i++) {
		exp[i].ts_ns = fifo_ts(i, 2, TEMP_PERIOD_NS);
		exp[i].micro[0] = temp_micro(temp[i]);
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_DIE_TEMP), 9, exp, 2);

	for (int i = 0; i < 2; i++) {
		exp[i].ts_ns = fifo_ts(i, 2, SFLP_PERIOD_NS);
		for (int j = 0; j < 4; j++) {
			exp[i].micro[j] = rot_micro[i][j];
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_GAME_ROTATION_VECTOR), 0, exp, 2);

	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 3; j++) {
			exp[i].micro[j] = gyro_micro(gbias[i][j], 0);
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_GBIAS_XYZ), 2, exp, 2);

	for (int i = 0; i < 2; i++) {
		for (int j = 0; j < 3; j++) {
			exp[i].micro[j] = accel_micro(gravity[i][j], 0);
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_GRAVITY_VECTOR), 5, exp, 2);

	check_no_trigger(fifo_buffer);

	check_unsupported(fifo_buffer, chan(SENSOR_CHAN_LIGHT));
	check_unsupported(fifo_buffer, chan(SENSOR_CHAN_HUMIDITY));
	check_unsupported(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 1});
	if (!IS_ENABLED(CONFIG_LSM6DSV16X_SENSORHUB)) {
		check_unsupported(fifo_buffer, chan(SENSOR_CHAN_MAGN_XYZ));
		check_unsupported(fifo_buffer, chan(SENSOR_CHAN_PRESS));
	}
}

ZTEST(lsm6dsv16x_decoder, test_fifo_rotation_identity)
{
	uint32_t fit = 0;

	/* x = y = z = 0, so w = 1, saturated to the largest q31 value */
	fifo_init();
	put_word(LSM6DSV16X_SFLP_GAME_ROTATION_VECTOR_TAG, 0, 0, 0);

	zassert_equal(
		decoder->decode(fifo_buffer, chan(SENSOR_CHAN_GAME_ROTATION_VECTOR), &fit, 1, &out),
		1);
	zassert_equal(out.rot.shift, 0);
	zassert_equal(out.rot.readings[0].x, 0);
	zassert_equal(out.rot.readings[0].y, 0);
	zassert_equal(out.rot.readings[0].z, 0);
	zassert_equal(out.rot.readings[0].w, INT32_MAX);
}

ZTEST(lsm6dsv16x_decoder, test_fifo_low_odr)
{
	struct expected exp[MAX_READINGS];
	const int n = 10;

	/* 10 readings at 1.875 Hz span more than 2^32 ns */
	fifo_init();
	fifo.hdr.accel_batch_odr = LSM6DSVXXX_DT_XL_BATCHED_AT_1Hz875;

	for (int i = 0; i < n; i++) {
		put_word(LSM6DSV16X_XL_NC_TAG, i, -i, 100);
		exp[i].ts_ns = fifo_ts(i, n, 533333333ULL);
		exp[i].micro[0] = accel_micro(i, FIFO_FS_IDX);
		exp[i].micro[1] = accel_micro(-i, FIFO_FS_IDX);
		exp[i].micro[2] = accel_micro(100, FIFO_FS_IDX);
	}

	check_readings(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 7, exp, n);

	/* Channels without words in the buffer */
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_GYRO_XYZ), -ENODATA);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_DIE_TEMP), -ENODATA);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_GAME_ROTATION_VECTOR), -ENODATA);
}

ZTEST(lsm6dsv16x_decoder, test_fifo_many_words)
{
	const int n = MAX_WORDS;
	uint16_t frame_count;
	uint32_t fit = 0;
	struct expected exp;

	/* More than 255 words of one kind */
	fifo_init();
	fifo.hdr.accel_batch_odr = LSM6DSVXXX_DT_XL_BATCHED_AT_7680Hz;

	for (int i = 0; i < n; i++) {
		put_word(LSM6DSV16X_XL_NC_TAG, i, 0, 0);
	}

	zassert_ok(
		decoder->get_frame_count(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &frame_count));
	zassert_equal(frame_count, n);

	for (int i = 0; i < n; i += MAX_READINGS) {
		int count = MIN(MAX_READINGS, n - i);

		zassert_equal(decoder->decode(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &fit,
					      MAX_READINGS, &out),
			      count);
		for (int k = 0; k < count; k++) {
			exp.ts_ns = fifo_ts(i + k, n, SENSOR_ODR_MHZ_TO_PERIOD_NS(7680000));
			exp.micro[0] = accel_micro(i + k, FIFO_FS_IDX);
			exp.micro[1] = 0;
			exp.micro[2] = 0;
			check_reading(chan(SENSOR_CHAN_ACCEL_XYZ), k, 7, &exp);
		}
	}
	zassert_equal(decoder->decode(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &fit, 1, &out), 0);
}

ZTEST(lsm6dsv16x_decoder, test_fifo_empty)
{
	/*
	 * Buffer completed without data for SENSOR_STREAM_DATA_NOP or _DROP: all fields but these
	 * are 0, so the batch rates are LSM6DSVXXX_DT_*_NOT_BATCHED.
	 */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.timestamp = LAST_TS_NS;
	fifo.hdr.int_status = 0x80;

	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 0);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_GYRO_Z), 0);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_DIE_TEMP), 0);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_GAME_ROTATION_VECTOR), 0);
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_GRAVITY_VECTOR), 0);
	check_no_trigger(fifo_buffer);
}

ZTEST(lsm6dsv16x_decoder, test_fifo_invalid_odr)
{
	uint16_t frame_count;
	uint32_t fit = 0;

	fifo_init();
	fifo.hdr.accel_batch_odr = 13;
	fifo.hdr.sflp_batch_odr = 6;
	put_word(LSM6DSV16X_XL_NC_TAG, 1, 2, 3);

	zassert_equal(
		decoder->get_frame_count(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), &frame_count),
		-EINVAL);
	zassert_equal(decoder->decode(fifo_buffer, chan(SENSOR_CHAN_ACCEL_X), &fit, 1, &out),
		      -EINVAL);
	zassert_equal(decoder->get_frame_count(fifo_buffer, chan(SENSOR_CHAN_GRAVITY_VECTOR),
					       &frame_count),
		      -EINVAL);
	zassert_ok(decoder->get_frame_count(fifo_buffer, chan(SENSOR_CHAN_GYRO_XYZ), &frame_count));
	zassert_equal(frame_count, 0);
}

#ifdef CONFIG_LSM6DSV16X_SENSORHUB

#define SHUB_PERIOD_NS 100000000ULL

static uint8_t shub_index(enum sensor_channel type)
{
	for (uint8_t k = 0; lsm6dsv16x_shub_type(k) != SENSOR_CHAN_COMMON_COUNT; k++) {
		if (lsm6dsv16x_shub_type(k) == type) {
			return k;
		}
	}

	ztest_test_fail();

	return 0;
}

static int64_t press_micro(uint32_t raw)
{
	/* 4096 LSB/hPa, in micro-kPa */
	return (int64_t)raw * 100000LL / 4096LL;
}

static void put_press_word(uint8_t tag, uint32_t raw)
{
	put_word(tag, raw & 0xffff, raw >> 16, 0);
}

ZTEST(lsm6dsv16x_decoder, test_fifo_shub)
{
	static const int16_t magn[2][3] = {{1000, -2000, 300}, {INT16_MIN, INT16_MAX, 0}};
	static const uint32_t press[2] = {4150272, 4096000};
	struct expected exp[2];

	fifo_init();
	/* Sensor hub targets 1 to 3: barometer, magnetometer, hygrometer */
	fifo.hdr.num_ext_dev = 3;
	fifo.hdr.shub_ext[0] = shub_index(SENSOR_CHAN_PRESS);
	fifo.hdr.shub_ext[1] = shub_index(SENSOR_CHAN_MAGN_XYZ);
	fifo.hdr.shub_ext[2] = shub_index(SENSOR_CHAN_HUMIDITY);

	put_press_word(LSM6DSV16X_SENSORHUB_SLAVE1_TAG, press[0]);
	put_word_3(LSM6DSV16X_SENSORHUB_SLAVE2_TAG, magn[0]);
	put_word(LSM6DSV16X_SENSORHUB_SLAVE3_TAG, 0x1234, 0x5678, 0);
	put_word(LSM6DSV16X_XL_NC_TAG, 1, 2, 3);
	/* Target 0 is used for configuration and is not batched */
	put_word(LSM6DSV16X_SENSORHUB_SLAVE0_TAG, 1, 2, 3);
	put_press_word(LSM6DSV16X_SENSORHUB_SLAVE1_TAG, press[1]);
	put_word_3(LSM6DSV16X_SENSORHUB_SLAVE2_TAG, magn[1]);

	for (int i = 0; i < 2; i++) {
		exp[i].ts_ns = fifo_ts(i, 2, SHUB_PERIOD_NS);
		for (int j = 0; j < 3; j++) {
			exp[i].micro[j] = (int64_t)magn[i][j] * 1500;
		}
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_MAGN_XYZ), 9, exp, 2);

	for (int i = 0; i < 2; i++) {
		exp[i].micro[0] = press_micro(press[i]);
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_PRESS), 8, exp, 2);

	/* Words of targets without an external device are skipped */
	fifo.hdr.num_ext_dev = 1;
	check_no_readings(fifo_buffer, chan(SENSOR_CHAN_MAGN_XYZ), -ENODATA);
	check_readings(fifo_buffer, chan(SENSOR_CHAN_PRESS), 8, exp, 2);

	exp[0].ts_ns = LAST_TS_NS;
	for (int j = 0; j < 3; j++) {
		exp[0].micro[j] = accel_micro(j + 1, FIFO_FS_IDX);
	}
	check_readings(fifo_buffer, chan(SENSOR_CHAN_ACCEL_XYZ), 7, exp, 1);

	check_unsupported(fifo_buffer, chan(SENSOR_CHAN_HUMIDITY));
}

#endif /* CONFIG_LSM6DSV16X_SENSORHUB */

static void *lsm6dsv16x_decoder_setup(void)
{
	/* The decoder is shared by all instances and not defined per compatible */
	zassert_ok(sensor_get_decoder(DEVICE_DT_GET(DT_NODELABEL(lsm6dsv16x)), &decoder));

	return NULL;
}

ZTEST_SUITE(lsm6dsv16x_decoder, NULL, lsm6dsv16x_decoder_setup, NULL, NULL, NULL);
