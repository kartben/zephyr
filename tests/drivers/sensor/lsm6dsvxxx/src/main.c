/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/dt-bindings/sensor/lsm6dsvxxx.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "lsm6dsvxxx.h"

#define DEV_320X DEVICE_DT_GET(DT_NODELABEL(lsm6dsv320x))
#define DEV_80X  DEVICE_DT_GET(DT_NODELABEL(lsm6dsv80x))
#define DEV_256X DEVICE_DT_GET(DT_NODELABEL(ism6hg256x))

#define TEST_PI   3.14159265358979323846
#define TEST_G    9.80665
#define SAMPLE_TS 123456789ULL

/* accel_fs and gyro_fs values of the encoded header */
#define ACCEL_FS_8G     2U
#define GYRO_FS_2000DPS 4U

/* Relative errors allowed: accelerometer sensitivities are truncated to micro-m/s^2 */
#define ACCEL_REL_TOL 1e-3
#define REL_TOL       1e-4

static const struct sensor_decoder_api *decoder;

static const struct sensor_chan_spec accel_xyz = {SENSOR_CHAN_ACCEL_XYZ, 0};
static const struct sensor_chan_spec gyro_xyz = {SENSOR_CHAN_GYRO_XYZ, 0};
static const struct sensor_chan_spec die_temp = {SENSOR_CHAN_DIE_TEMP, 0};
static const struct sensor_chan_spec game_rot = {SENSOR_CHAN_GAME_ROTATION_VECTOR, 0};
static const struct sensor_chan_spec gravity = {SENSOR_CHAN_GRAVITY_VECTOR, 0};
static const struct sensor_chan_spec gbias = {SENSOR_CHAN_GBIAS_XYZ, 0};

/* Nominal accelerometer sensitivities in micro-g/LSB, indexed by accel_fs */
static const double lsm6dsv320x_accel_ug[] = {61, 122, 244, 488, 976, 1952, 3904, 7808, 10417};
static const double lsm6dsv80x_accel_ug[] = {61, 122, 244, 488, 976, 1952, 3904};
static const double ism6hg256x_accel_ug[] = {61, 122, 244, 488, 976, 1952, 3904, 7808};

static double q31_to_double(q31_t value, int8_t shift)
{
	return ldexp((double)value, shift - 31);
}

static double accel_ms2(int16_t raw, double ug_per_lsb)
{
	return raw * ug_per_lsb * 1e-6 * TEST_G;
}

static double gyro_rads(int16_t raw, uint8_t fs)
{
	return raw * 4.375e-3 * (1 << fs) * TEST_PI / 180.0;
}

static double temp_celsius(int16_t raw)
{
	return 25.0 + raw / 355.0;
}

static void check_value(q31_t value, int8_t shift, double expected, double rel_tol)
{
	double actual = q31_to_double(value, shift);
	double tol = fabs(expected) * rel_tol + ldexp(1.0, shift - 31) * 2.0;

	zassert_true(fabs(actual - expected) <= tol, "got %f, expected %f", actual, expected);
}

static void check_size(struct sensor_chan_spec chan, int rc, size_t base, size_t frame)
{
	size_t base_size;
	size_t frame_size;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), rc, "chan %d",
		      chan.chan_type);
	if (rc == 0) {
		zassert_equal(base_size, base, "chan %d", chan.chan_type);
		zassert_equal(frame_size, frame, "chan %d", chan.chan_type);
	}
}

static void check_count(const uint8_t *buffer, struct sensor_chan_spec chan, int rc, uint16_t count)
{
	uint16_t frame_count = UINT16_MAX;

	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), rc, "chan %d",
		      chan.chan_type);
	if (rc == 0) {
		zassert_equal(frame_count, count, "chan %d", chan.chan_type);
	}
}

static void check_unsupported(const uint8_t *buffer, struct sensor_chan_spec chan)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	check_size(chan, -ENOTSUP, 0, 0);
	check_count(buffer, chan, -ENOTSUP, 0);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

static void put_xyz(uint8_t *dst, int16_t x, int16_t y, int16_t z)
{
	sys_put_le16((uint16_t)x, &dst[0]);
	sys_put_le16((uint16_t)y, &dst[2]);
	sys_put_le16((uint16_t)z, &dst[4]);
}

static void sample_init(struct lsm6dsvxxx_rtio_data *sample, const struct device *dev,
			uint8_t accel_fs, uint8_t gyro_fs)
{
	memset(sample, 0, sizeof(*sample));
	sample->header.cfg = dev->config;
	sample->header.timestamp = SAMPLE_TS;
	sample->header.accel_fs = accel_fs;
	sample->header.gyro_fs = gyro_fs;
}

ZTEST(lsm6dsvxxx_decoder, test_size_info)
{
	const size_t q31_base = sizeof(struct sensor_q31_data);
	const size_t q31_frame = sizeof(struct sensor_q31_sample_data);
	const size_t xyz_base = sizeof(struct sensor_three_axis_data);
	const size_t xyz_frame = sizeof(struct sensor_three_axis_sample_data);
	const int stream_rc = IS_ENABLED(CONFIG_LSM6DSVXXX_STREAM) ? 0 : -ENOTSUP;

	check_size(accel_xyz, 0, xyz_base, xyz_frame);
	check_size((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_X, 0}, 0, q31_base, q31_frame);
	check_size(gyro_xyz, 0, xyz_base, xyz_frame);
	check_size((struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0}, 0, q31_base, q31_frame);
	check_size(die_temp, IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP) ? 0 : -ENOTSUP, q31_base,
		   q31_frame);
	check_size(game_rot, stream_rc, sizeof(struct sensor_game_rotation_vector_data),
		   sizeof(struct sensor_game_rotation_vector_sample_data));
	check_size(gravity, stream_rc, xyz_base, xyz_frame);
	check_size(gbias, stream_rc, xyz_base, xyz_frame);
	check_size((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0}, -ENOTSUP, 0, 0);
	check_size((struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1}, -ENOTSUP, 0, 0);
}

ZTEST(lsm6dsvxxx_decoder, test_single_sample)
{
	struct lsm6dsvxxx_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	const int16_t accel[] = {1000, -2000, 16384};
	const int16_t gyro[] = {100, -200, 30000};
	const int16_t temp = -1775;
	const struct sensor_chan_spec sflp_chans[] = {game_rot, gravity, gbias};
	struct sensor_three_axis_data out;
	struct sensor_q31_data out_q31;
	uint32_t fit = 0;

	sample_init(&sample, DEV_320X, ACCEL_FS_8G, GYRO_FS_2000DPS);
	sample.has_accel = 1;
	sample.has_gyro = 1;
	sample.has_temp = IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP) ? 1 : 0;
	put_xyz((uint8_t *)sample.accel, accel[0], accel[1], accel[2]);
	put_xyz((uint8_t *)sample.gyro, gyro[0], gyro[1], gyro[2]);
	sys_put_le16((uint16_t)temp, (uint8_t *)&sample.temp);

	check_count(buffer, accel_xyz, 0, 1);
	check_count(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, 0, 1);
	check_count(buffer, gyro_xyz, 0, 1);
	check_count(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0}, 0, 1);

	/* Accelerometer */
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	zassert_equal(out.shift, 7);
	for (int i = 0; i < 3; i++) {
		check_value(out.readings[0].values[i], out.shift, accel_ms2(accel[i], 244),
			    ACCEL_REL_TOL);
	}
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 0);

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0},
				      &fit, 1, &out_q31),
		      1);
	zassert_equal(out_q31.header.base_timestamp_ns, SAMPLE_TS);
	zassert_equal(out_q31.shift, 7);
	check_value(out_q31.readings[0].value, out_q31.shift, accel_ms2(accel[1], 244),
		    ACCEL_REL_TOL);

	/* Gyroscope */
	fit = 0;
	zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, SAMPLE_TS);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.shift, 6);
	for (int i = 0; i < 3; i++) {
		check_value(out.readings[0].values[i], out.shift,
			    gyro_rads(gyro[i], GYRO_FS_2000DPS), REL_TOL);
	}

	fit = 0;
	zassert_equal(decoder->decode(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_Z, 0},
				      &fit, 1, &out_q31),
		      1);
	check_value(out_q31.readings[0].value, out_q31.shift, gyro_rads(gyro[2], GYRO_FS_2000DPS),
		    REL_TOL);

	/* Temperature */
	if (IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP)) {
		check_count(buffer, die_temp, 0, 1);
		fit = 0;
		zassert_equal(decoder->decode(buffer, die_temp, &fit, 1, &out_q31), 1);
		zassert_equal(out_q31.header.base_timestamp_ns, SAMPLE_TS);
		zassert_equal(out_q31.shift, 9);
		check_value(out_q31.readings[0].temperature, out_q31.shift, temp_celsius(temp),
			    REL_TOL);
	} else {
		check_unsupported(buffer, die_temp);
	}

	/* Fusion outputs are only in FIFO data */
	for (size_t i = 0; i < ARRAY_SIZE(sflp_chans); i++) {
		fit = 0;
		check_count(buffer, sflp_chans[i], -ENOTSUP, 0);
		zassert_equal(decoder->decode(buffer, sflp_chans[i], &fit, 1, &out), -ENOTSUP);
	}

	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buffer, SENSOR_TRIG_FIFO_WATERMARK));

	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	check_unsupported(buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 1});
}

ZTEST(lsm6dsvxxx_decoder, test_single_accel_only)
{
	struct lsm6dsvxxx_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* Data ready stream buffer: only the accelerometer is read */
	sample_init(&sample, DEV_320X, 0U, 0U);
	sample.has_accel = 1;
	put_xyz((uint8_t *)sample.accel, -16384, 0, 16384);

	/* Sensors that were not read have no reading */
	check_count(buffer, accel_xyz, 0, 1);
	check_count(buffer, gyro_xyz, 0, 0);
	zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, 1, &out), -ENODATA);
	if (IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP)) {
		check_count(buffer, die_temp, 0, 0);
		zassert_equal(decoder->decode(buffer, die_temp, &fit, 1, &out), -ENODATA);
	}

	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 0, &out), 0);
	zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 1);
	zassert_equal(out.shift, 5);
	check_value(out.readings[0].x, out.shift, accel_ms2(-16384, 61), ACCEL_REL_TOL);
	check_value(out.readings[0].y, out.shift, 0.0, 0.0);
	check_value(out.readings[0].z, out.shift, accel_ms2(16384, 61), ACCEL_REL_TOL);
}

static void check_accel_scales(const struct device *dev, const double *ug, size_t num)
{
	const int16_t raws[] = {INT16_MAX, INT16_MIN, 12345, -1};
	struct lsm6dsvxxx_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;

	for (uint8_t fs = 0; fs < num; fs++) {
		for (size_t r = 0; r < ARRAY_SIZE(raws); r++) {
			uint32_t fit = 0;

			sample_init(&sample, dev, fs, 0U);
			sample.has_accel = 1;
			put_xyz((uint8_t *)sample.accel, raws[r], -raws[r] / 2, 0);

			zassert_equal(decoder->decode(buffer, accel_xyz, &fit, 1, &out), 1);
			zassert_equal(out.shift, 5 + fs);
			check_value(out.readings[0].x, out.shift, accel_ms2(raws[r], ug[fs]),
				    ACCEL_REL_TOL);
			check_value(out.readings[0].y, out.shift, accel_ms2(-raws[r] / 2, ug[fs]),
				    ACCEL_REL_TOL);
		}
	}
}

ZTEST(lsm6dsvxxx_decoder, test_accel_full_scales)
{
	check_accel_scales(DEV_320X, lsm6dsv320x_accel_ug, ARRAY_SIZE(lsm6dsv320x_accel_ug));
	check_accel_scales(DEV_80X, lsm6dsv80x_accel_ug, ARRAY_SIZE(lsm6dsv80x_accel_ug));
	check_accel_scales(DEV_256X, ism6hg256x_accel_ug, ARRAY_SIZE(ism6hg256x_accel_ug));
}

ZTEST(lsm6dsvxxx_decoder, test_gyro_full_scales)
{
	const int16_t raws[] = {INT16_MAX, INT16_MIN, -12345, 1};
	struct lsm6dsvxxx_rtio_data sample;
	const uint8_t *buffer = (const uint8_t *)&sample;
	struct sensor_three_axis_data out;
	uint32_t fit;

	for (uint8_t fs = 0; fs <= 5U; fs++) {
		for (size_t r = 0; r < ARRAY_SIZE(raws); r++) {
			fit = 0;
			sample_init(&sample, DEV_80X, 0U, fs);
			sample.has_gyro = 1;
			put_xyz((uint8_t *)sample.gyro, 0, raws[r], 0);

			zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, 1, &out), 1);
			zassert_equal(out.shift, 2 + fs);
			check_value(out.readings[0].y, out.shift, gyro_rads(raws[r], fs), REL_TOL);
		}
	}

	/* Invalid full scale */
	fit = 0;
	sample_init(&sample, DEV_80X, 0U, 6U);
	sample.has_gyro = 1;
	zassert_equal(decoder->decode(buffer, gyro_xyz, &fit, 1, &out), -EINVAL);
	check_count(buffer, gyro_xyz, -EINVAL, 0);
}

#ifdef CONFIG_LSM6DSVXXX_STREAM

#define FIFO_TS        1000000000ULL
#define FIFO_MAX_WORDS 300
#define XL_PERIOD_NS   16666666ULL /* 60 Hz */
#define GY_PERIOD_NS   8333333ULL  /* 120 Hz */
#define TEMP_PERIOD_NS 66666666ULL /* 15 Hz */
#define SFLP_PERIOD_NS 33333333ULL /* 30 Hz */

/* Half-precision floats */
#define F16_0    0x0000U
#define F16_0_25 0x3400U
#define F16_0_5  0x3800U
#define F16_M0_5 0xB800U
#define F16_1    0x3C00U

static struct {
	struct lsm6dsvxxx_fifo_data hdr;
	uint8_t words[FIFO_MAX_WORDS][LSM6DSVXXX_FIFO_ITEM_LEN];
} __packed fifo;

static const uint8_t *const fifo_buffer = (const uint8_t *)&fifo;

static void fifo_init(uint16_t count)
{
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.cfg = DEV_320X->config;
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.accel_fs = ACCEL_FS_8G;
	fifo.hdr.header.gyro_fs = GYRO_FS_2000DPS;
	fifo.hdr.header.timestamp = FIFO_TS;
	fifo.hdr.fifo_count = count;
	fifo.hdr.accel_batch_odr = LSM6DSVXXX_DT_XL_BATCHED_AT_60Hz;
	fifo.hdr.gyro_batch_odr = LSM6DSVXXX_DT_GY_BATCHED_AT_120Hz;
	fifo.hdr.temp_batch_odr = LSM6DSVXXX_DT_TEMP_BATCHED_AT_15Hz;
	fifo.hdr.sflp_batch_odr = LSM6DSVXXX_DT_SFLP_ODR_AT_30Hz;
}

static void put_word(int idx, uint8_t tag, uint16_t x, uint16_t y, uint16_t z)
{
	/* The low bits of the TAG byte hold the tag counter and parity */
	fifo.words[idx][0] = (uint8_t)((tag << 3) | 0x5U);
	put_xyz(&fifo.words[idx][1], (int16_t)x, (int16_t)y, (int16_t)z);
}

/* Interleaved FIFO words: 3 accel, 2 gyro, 2 temp, 3 game rotation, 1 gravity, 1 gbias */
static const int16_t fifo_accel[3][3] = {{100, 200, 300}, {-100, -200, -300}, {1000, 2000, 3000}};
static const int16_t fifo_gyro[2][3] = {{10, 20, 30}, {-10, -20, -30}};
static const int16_t fifo_temp[2] = {355, -710};
static const int16_t fifo_gravity[3] = {0, -16393, 1000};
static const int16_t fifo_gbias[3] = {100, -100, 1000};

static void fifo_fill(void)
{
	fifo_init(13);
	put_word(0, LSM6DSVXXX_XL_NC_TAG, fifo_accel[0][0], fifo_accel[0][1], fifo_accel[0][2]);
	put_word(1, LSM6DSVXXX_GY_NC_TAG, fifo_gyro[0][0], fifo_gyro[0][1], fifo_gyro[0][2]);
	put_word(2, LSM6DSVXXX_TEMPERATURE_TAG, fifo_temp[0], 0, 0);
	put_word(3, LSM6DSVXXX_TIMESTAMP_TAG, 0x1234, 0x5678, 0x9abc);
	put_word(4, LSM6DSVXXX_SFLP_GAME_ROTATION_VECTOR_TAG, F16_0_5, F16_M0_5, F16_0_25);
	put_word(5, LSM6DSVXXX_SFLP_GRAVITY_VECTOR_TAG, fifo_gravity[0], fifo_gravity[1],
		 fifo_gravity[2]);
	put_word(6, LSM6DSVXXX_SFLP_GYROSCOPE_BIAS_TAG, fifo_gbias[0], fifo_gbias[1],
		 fifo_gbias[2]);
	put_word(7, LSM6DSVXXX_XL_HG_TAG, fifo_accel[1][0], fifo_accel[1][1], fifo_accel[1][2]);
	put_word(8, LSM6DSVXXX_GY_NC_TAG, fifo_gyro[1][0], fifo_gyro[1][1], fifo_gyro[1][2]);
	put_word(9, LSM6DSVXXX_TEMPERATURE_TAG, fifo_temp[1], 0, 0);
	put_word(10, LSM6DSVXXX_SFLP_GAME_ROTATION_VECTOR_TAG, F16_0, F16_0, F16_0);
	put_word(11, LSM6DSVXXX_XL_NC_TAG, fifo_accel[2][0], fifo_accel[2][1], fifo_accel[2][2]);
	put_word(12, LSM6DSVXXX_SFLP_GAME_ROTATION_VECTOR_TAG, F16_1, F16_1, F16_0);
}

/* Expected game rotation vectors (x, y, z, w) */
static const double fifo_game_rot[3][4] = {
	{0.5, -0.5, 0.25, 0.6614378277661477},
	{0.0, 0.0, 0.0, 1.0},
	{0.7071067811865476, 0.7071067811865476, 0.0, 0.0},
};

ZTEST(lsm6dsvxxx_decoder, test_fifo_frame_count)
{
	fifo_fill();

	check_count(fifo_buffer, accel_xyz, 0, 3);
	check_count(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Y, 0}, 0, 3);
	check_count(fifo_buffer, gyro_xyz, 0, 2);
	check_count(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_X, 0}, 0, 2);
	check_count(fifo_buffer, die_temp, IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP) ? 0 : -ENOTSUP,
		    2);
	check_count(fifo_buffer, game_rot, 0, 3);
	check_count(fifo_buffer, gravity, 0, 1);
	check_count(fifo_buffer, gbias, 0, 1);

	zassert_false(decoder->has_trigger(fifo_buffer, SENSOR_TRIG_FIFO_WATERMARK));
	zassert_false(decoder->has_trigger(fifo_buffer, SENSOR_TRIG_FIFO_FULL));

	check_unsupported(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 0});
	check_unsupported(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_XYZ, 1});
	if (!IS_ENABLED(CONFIG_LSM6DSVXXX_ENABLE_TEMP)) {
		check_unsupported(fifo_buffer, die_temp);
	}
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_accel)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[2];
	} out;
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[2];
	} out_q31;
	const struct sensor_three_axis_sample_data *out_rd = out.data.readings;
	const struct sensor_q31_sample_data *q31_rd = out_q31.data.readings;
	uint32_t fit = 0;

	fifo_fill();

	zassert_equal(decoder->decode(fifo_buffer, accel_xyz, &fit, 3, &out), 3);
	zassert_equal(out.data.header.reading_count, 3);
	zassert_equal(out.data.header.base_timestamp_ns, FIFO_TS - 2 * XL_PERIOD_NS);
	zassert_equal(out.data.shift, 7);
	for (int i = 0; i < 3; i++) {
		zassert_equal(out_rd[i].timestamp_delta, i * XL_PERIOD_NS);
		for (int a = 0; a < 3; a++) {
			check_value(out_rd[i].values[a], out.data.shift,
				    accel_ms2(fifo_accel[i][a], 244), ACCEL_REL_TOL);
		}
	}
	zassert_equal(decoder->decode(fifo_buffer, accel_xyz, &fit, 3, &out), 0);

	/* One reading per call */
	fit = 0;
	for (int i = 0; i < 3; i++) {
		zassert_equal(decoder->decode(fifo_buffer, accel_xyz, &fit, 1, &out), 1);
		zassert_equal(out.data.header.reading_count, 1);
		zassert_equal(out.data.header.base_timestamp_ns +
				      out.data.readings[0].timestamp_delta,
			      FIFO_TS - (2 - i) * XL_PERIOD_NS);
		check_value(out.data.readings[0].z, out.data.shift,
			    accel_ms2(fifo_accel[i][2], 244), ACCEL_REL_TOL);
	}
	zassert_equal(decoder->decode(fifo_buffer, accel_xyz, &fit, 1, &out), 0);

	/* Single axis */
	fit = 0;
	zassert_equal(decoder->decode(fifo_buffer,
				      (struct sensor_chan_spec){SENSOR_CHAN_ACCEL_Z, 0}, &fit, 3,
				      &out_q31),
		      3);
	zassert_equal(out_q31.data.shift, 7);
	zassert_equal(out_q31.data.header.base_timestamp_ns, FIFO_TS - 2 * XL_PERIOD_NS);
	for (int i = 0; i < 3; i++) {
		check_value(q31_rd[i].value, out_q31.data.shift, accel_ms2(fifo_accel[i][2], 244),
			    ACCEL_REL_TOL);
	}
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_gyro)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[1];
	} out;
	struct sensor_q31_data out_q31;
	const struct sensor_three_axis_sample_data *out_rd = out.data.readings;
	uint32_t fit = 0;

	fifo_fill();

	zassert_equal(decoder->decode(fifo_buffer, gyro_xyz, &fit, 2, &out), 2);
	zassert_equal(out.data.header.reading_count, 2);
	zassert_equal(out.data.header.base_timestamp_ns, FIFO_TS - GY_PERIOD_NS);
	zassert_equal(out.data.shift, 6);
	for (int i = 0; i < 2; i++) {
		zassert_equal(out_rd[i].timestamp_delta, i * GY_PERIOD_NS);
		for (int a = 0; a < 3; a++) {
			check_value(out_rd[i].values[a], out.data.shift,
				    gyro_rads(fifo_gyro[i][a], GYRO_FS_2000DPS), REL_TOL);
		}
	}

	fit = 0;
	for (int i = 0; i < 2; i++) {
		zassert_equal(decoder->decode(fifo_buffer,
					      (struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0},
					      &fit, 1, &out_q31),
			      1);
		zassert_equal(out_q31.header.base_timestamp_ns +
				      out_q31.readings[0].timestamp_delta,
			      FIFO_TS - (1 - i) * GY_PERIOD_NS);
		check_value(out_q31.readings[0].value, out_q31.shift,
			    gyro_rads(fifo_gyro[i][1], GYRO_FS_2000DPS), REL_TOL);
	}
	zassert_equal(decoder->decode(fifo_buffer, (struct sensor_chan_spec){SENSOR_CHAN_GYRO_Y, 0},
				      &fit, 1, &out_q31),
		      0);
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_temp)
{
	struct {
		struct sensor_q31_data data;
		struct sensor_q31_sample_data extra[1];
	} out;
	const struct sensor_q31_sample_data *out_rd = out.data.readings;
	uint32_t fit = 0;

	Z_TEST_SKIP_IFNDEF(CONFIG_LSM6DSVXXX_ENABLE_TEMP);

	fifo_fill();

	zassert_equal(decoder->decode(fifo_buffer, die_temp, &fit, 2, &out), 2);
	zassert_equal(out.data.header.reading_count, 2);
	zassert_equal(out.data.header.base_timestamp_ns, FIFO_TS - TEMP_PERIOD_NS);
	zassert_equal(out.data.shift, 9);
	for (int i = 0; i < 2; i++) {
		zassert_equal(out_rd[i].timestamp_delta, i * TEMP_PERIOD_NS);
		check_value(out_rd[i].temperature, out.data.shift, temp_celsius(fifo_temp[i]),
			    REL_TOL);
	}
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_sflp)
{
	struct {
		struct sensor_game_rotation_vector_data data;
		struct sensor_game_rotation_vector_sample_data extra[2];
	} out_rot;
	struct sensor_three_axis_data out;
	const struct sensor_game_rotation_vector_sample_data *rot_rd = out_rot.data.readings;
	uint32_t fit = 0;

	fifo_fill();

	zassert_equal(decoder->decode(fifo_buffer, game_rot, &fit, 3, &out_rot), 3);
	zassert_equal(out_rot.data.header.reading_count, 3);
	zassert_equal(out_rot.data.header.base_timestamp_ns, FIFO_TS - 2 * SFLP_PERIOD_NS);
	zassert_equal(out_rot.data.shift, 0);
	for (int i = 0; i < 3; i++) {
		zassert_equal(rot_rd[i].timestamp_delta, i * SFLP_PERIOD_NS);
		for (int a = 0; a < 4; a++) {
			check_value(rot_rd[i].values[a], out_rot.data.shift, fifo_game_rot[i][a],
				    1e-6);
		}
	}

	fit = 0;
	for (int i = 0; i < 3; i++) {
		zassert_equal(decoder->decode(fifo_buffer, game_rot, &fit, 1, &out_rot), 1);
		zassert_equal(out_rot.data.header.base_timestamp_ns +
				      out_rot.data.readings[0].timestamp_delta,
			      FIFO_TS - (2 - i) * SFLP_PERIOD_NS);
		check_value(out_rot.data.readings[0].w, 0, fifo_game_rot[i][3], 1e-6);
	}

	/* Gravity vector in mg */
	fit = 0;
	zassert_equal(decoder->decode(fifo_buffer, gravity, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, FIFO_TS);
	zassert_equal(out.shift, 12);
	for (int a = 0; a < 3; a++) {
		check_value(out.readings[0].values[a], out.shift, fifo_gravity[a] * 0.061, REL_TOL);
	}
	zassert_equal(decoder->decode(fifo_buffer, gravity, &fit, 1, &out), 0);

	/* Gyroscope bias at 125 dps full scale */
	fit = 0;
	zassert_equal(decoder->decode(fifo_buffer, gbias, &fit, 1, &out), 1);
	zassert_equal(out.header.base_timestamp_ns, FIFO_TS);
	zassert_equal(out.shift, 2);
	for (int a = 0; a < 3; a++) {
		check_value(out.readings[0].values[a], out.shift, gyro_rads(fifo_gbias[a], 0),
			    REL_TOL);
	}
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_many_words)
{
	struct {
		struct sensor_three_axis_data data;
		struct sensor_three_axis_sample_data extra[63];
	} out;
	uint64_t expected_ns = FIFO_TS - (FIFO_MAX_WORDS - 1) * XL_PERIOD_NS;
	const struct sensor_three_axis_sample_data *out_rd = out.data.readings;
	uint32_t fit = 0;
	int total = 0;
	int rc;

	fifo_init(FIFO_MAX_WORDS);
	for (int i = 0; i < FIFO_MAX_WORDS; i++) {
		put_word(i, LSM6DSVXXX_XL_NC_TAG, i, -i, 0);
	}

	check_count(fifo_buffer, accel_xyz, 0, FIFO_MAX_WORDS);

	/* No gyroscope word */
	check_count(fifo_buffer, gyro_xyz, 0, 0);
	zassert_equal(decoder->decode(fifo_buffer, gyro_xyz, &fit, 64, &out), -ENODATA);
	zassert_equal(fit, 0);

	while ((rc = decoder->decode(fifo_buffer, accel_xyz, &fit, 64, &out)) > 0) {
		for (int i = 0; i < rc; i++) {
			zassert_equal(out.data.header.base_timestamp_ns + out_rd[i].timestamp_delta,
				      expected_ns);
			check_value(out_rd[i].x, out.data.shift, accel_ms2(total + i, 244),
				    ACCEL_REL_TOL);
			expected_ns += XL_PERIOD_NS;
		}
		total += rc;
	}

	zassert_equal(rc, 0);
	zassert_equal(total, FIFO_MAX_WORDS);
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_empty)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	/* Buffer completed without data, for SENSOR_STREAM_DATA_NOP or _DROP */
	memset(&fifo, 0, sizeof(fifo));
	fifo.hdr.header.cfg = DEV_320X->config;
	fifo.hdr.header.is_fifo = 1;
	fifo.hdr.header.timestamp = FIFO_TS;
	fifo.hdr.header.int_status = 0x80;

	check_count(fifo_buffer, accel_xyz, 0, 0);
	check_count(fifo_buffer, gyro_xyz, 0, 0);
	check_count(fifo_buffer, game_rot, 0, 0);
	check_count(fifo_buffer, gravity, 0, 0);
	check_count(fifo_buffer, gbias, 0, 0);
	zassert_equal(decoder->decode(fifo_buffer, accel_xyz, &fit, 1, &out), 0);
	zassert_equal(decoder->decode(fifo_buffer, gbias, &fit, 1, &out), 0);
	zassert_false(decoder->has_trigger(fifo_buffer, SENSOR_TRIG_FIFO_WATERMARK));
}

ZTEST(lsm6dsvxxx_decoder, test_fifo_invalid_odr)
{
	struct sensor_three_axis_data out;
	uint32_t fit = 0;

	fifo_fill();
	fifo.hdr.sflp_batch_odr = 7;

	check_count(fifo_buffer, gravity, -EINVAL, 0);
	zassert_equal(decoder->decode(fifo_buffer, gravity, &fit, 1, &out), -EINVAL);
	check_count(fifo_buffer, accel_xyz, 0, 3);
}

#endif /* CONFIG_LSM6DSVXXX_STREAM */

static void *lsm6dsvxxx_decoder_setup(void)
{
	zassert_ok(sensor_get_decoder(DEV_320X, &decoder));

	return NULL;
}

ZTEST_SUITE(lsm6dsvxxx_decoder, NULL, lsm6dsvxxx_decoder_setup, NULL, NULL, NULL);
