/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor_clock.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/ztest.h>

#include "akm09918c.h"
#include "akm09918c_emul.h"
#include "akm09918c_reg.h"

/* Allowed error of a decoded value, in micro-Gauss */
#define TOLERANCE_UGAUSS 1

#define TIMESTAMP_NS 123456789012ULL

static const struct sensor_decoder_api *decoder = SENSOR_DECODER_DT_GET(DT_NODELABEL(akm09918c));

static const struct sensor_chan_spec chan_xyz = {SENSOR_CHAN_MAGN_XYZ, 0};

static const struct sensor_chan_spec chan_axis[] = {
	{SENSOR_CHAN_MAGN_X, 0},
	{SENSOR_CHAN_MAGN_Y, 0},
	{SENSOR_CHAN_MAGN_Z, 0},
};

static const int16_t raw_values[] = {AKM09918C_MAGN_MAX_DATA_REG, AKM09918C_MAGN_MIN_DATA_REG,
				     -12345};

static struct akm09918c_encoded_data edata;

static int64_t q31_to_micro(q31_t value, int8_t shift)
{
	return ((int64_t)value * 1000000) >> (31 - shift);
}

static void check_value(q31_t value, int8_t shift, int16_t raw)
{
	int64_t expected = raw * AKM09918C_MICRO_GAUSS_PER_BIT;
	int64_t actual = q31_to_micro(value, shift);

	zassert_equal(shift, 6);
	zassert_within(actual, expected, TOLERANCE_UGAUSS, "raw %d: got %lld, expected %lld", raw,
		       actual, expected);
}

static void fill_edata(const int16_t raw[3])
{
	/* Raw register bytes HXL to HZH as read from the sensor */
	uint8_t *data = (uint8_t *)&edata.reading.data;

	memset(&edata, 0, sizeof(edata));
	edata.header.timestamp = TIMESTAMP_NS;
	edata.reading.st1 = AKM09918C_ST1_DRDY;

	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)raw[i], &data[i * 2]);
	}
}

static void check_unsupported(struct sensor_chan_spec chan)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	size_t base_size;
	size_t frame_size;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_equal(decoder->get_size_info(chan, &base_size, &frame_size), -ENOTSUP);
	zassert_equal(decoder->get_frame_count(buffer, chan, &frame_count), -ENOTSUP);
	zassert_equal(decoder->decode(buffer, chan, &fit, 1, &out), -ENOTSUP);
}

ZTEST(akm09918c_decoder, test_size_info)
{
	size_t base_size;
	size_t frame_size;

	zassert_ok(decoder->get_size_info(chan_xyz, &base_size, &frame_size));
	zassert_equal(base_size, sizeof(struct sensor_three_axis_data));
	zassert_equal(frame_size, sizeof(struct sensor_three_axis_sample_data));

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		zassert_ok(decoder->get_size_info(chan_axis[i], &base_size, &frame_size));
		zassert_equal(base_size, sizeof(struct sensor_q31_data));
		zassert_equal(frame_size, sizeof(struct sensor_q31_sample_data));
	}
}

ZTEST(akm09918c_decoder, test_decode_xyz)
{
	const uint8_t *buffer = (const uint8_t *)&edata;
	struct sensor_three_axis_data out;
	uint16_t frame_count;
	uint32_t fit = 0;

	fill_edata(raw_values);

	zassert_ok(decoder->get_frame_count(buffer, chan_xyz, &frame_count));
	zassert_equal(frame_count, 1);

	/* Stale data in the output buffer must be overwritten */
	memset(&out, 0xa5, sizeof(out));
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 8, &out), 1);
	zassert_equal(out.header.reading_count, 1);
	zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	check_value(out.readings[0].x, out.shift, raw_values[0]);
	check_value(out.readings[0].y, out.shift, raw_values[1]);
	check_value(out.readings[0].z, out.shift, raw_values[2]);

	/* All readings have been decoded */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 8, &out), 0);

	/* One by one */
	fit = 0;
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	check_value(out.readings[0].z, out.shift, raw_values[2]);
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 0);
}

ZTEST(akm09918c_decoder, test_decode_single_axis)
{
	static const int16_t raw_small[] = {1, -1, 0};
	const uint8_t *buffer = (const uint8_t *)&edata;

	for (int i = 0; i < ARRAY_SIZE(chan_axis); i++) {
		for (int j = 0; j < 2; j++) {
			const int16_t *raw = (j == 0) ? raw_values : raw_small;
			struct sensor_q31_data out;
			uint16_t frame_count;
			uint32_t fit = 0;

			fill_edata(raw);

			zassert_ok(decoder->get_frame_count(buffer, chan_axis[i], &frame_count));
			zassert_equal(frame_count, 1);

			memset(&out, 0xa5, sizeof(out));
			zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out), 1);
			zassert_equal(out.header.reading_count, 1);
			zassert_equal(out.header.base_timestamp_ns, TIMESTAMP_NS);
			zassert_equal(out.readings[0].timestamp_delta, 0);
			check_value(out.readings[0].value, out.shift, raw[i]);
			zassert_equal(decoder->decode(buffer, chan_axis[i], &fit, 1, &out), 0);
		}
	}
}

ZTEST(akm09918c_decoder, test_unsupported)
{
	fill_edata(raw_values);

	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_GYRO_XYZ, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_ALL, 0});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_XYZ, 1});
	check_unsupported((struct sensor_chan_spec){SENSOR_CHAN_MAGN_X, 1});

	/* The sensor has no trigger */
	zassert_is_null(decoder->has_trigger);
}

RTIO_DEFINE(akm09918c_rtio, 1, 1);
SENSOR_DT_READ_IODEV(akm09918c_iodev, DT_NODELABEL(akm09918c), {SENSOR_CHAN_MAGN_XYZ, 0});

ZTEST(akm09918c_decoder, test_read_decode)
{
	static const int16_t raw[] = {1000, -2000, 32000};
	const struct emul *target = EMUL_DT_GET(DT_NODELABEL(akm09918c));
	uint8_t buffer[sizeof(struct akm09918c_encoded_data)];
	uint8_t st1 = AKM09918C_ST1_DRDY;
	struct sensor_three_axis_data out;
	uint8_t regs[6];
	uint64_t start_ns;
	uint64_t cycles;
	uint32_t fit = 0;

	akm09918c_emul_reset(target);
	akm09918c_emul_set_reg(target, AKM09918C_REG_ST1, &st1, 1);
	for (int i = 0; i < 3; i++) {
		sys_put_le16((uint16_t)raw[i], &regs[i * 2]);
	}
	akm09918c_emul_set_reg(target, AKM09918C_REG_HXL, regs, sizeof(regs));

	zassert_ok(sensor_clock_get_cycles(&cycles));
	start_ns = sensor_clock_cycles_to_ns(cycles);

	zassert_ok(sensor_read(&akm09918c_iodev, &akm09918c_rtio, buffer, sizeof(buffer)));

	/* The timestamp is taken when the measurement is started */
	zassert_equal(decoder->decode(buffer, chan_xyz, &fit, 1, &out), 1);
	zassert_between_inclusive(out.header.base_timestamp_ns, start_ns,
				  start_ns + AKM09918C_MEASURE_TIME_US * 1000ULL);
	zassert_equal(out.readings[0].timestamp_delta, 0);
	check_value(out.readings[0].x, out.shift, raw[0]);
	check_value(out.readings[0].y, out.shift, raw[1]);
	check_value(out.readings[0].z, out.shift, raw[2]);
}

ZTEST_SUITE(akm09918c_decoder, NULL, NULL, NULL, NULL, NULL);
