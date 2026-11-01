/*
 * Copyright (c) 2023 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/fff.h>
#include <zephyr/ztest.h>

#include "icm4268x_decoder.h"
#include "icm4268x_emul.h"
#include "icm4268x_reg.h"

#define NODE DT_NODELABEL(icm42688)

DEFINE_FFF_GLOBALS;

struct icm42688_fixture {
	const struct device *dev;
	const struct emul *target;
};

static void *icm42688_setup(void)
{
	static struct icm42688_fixture fixture = {
		.dev = DEVICE_DT_GET(DT_NODELABEL(icm42688)),
		.target = EMUL_DT_GET(DT_NODELABEL(icm42688)),
	};

	zassert_not_null(fixture.dev);
	zassert_not_null(fixture.target);
	return &fixture;
}

ZTEST_SUITE(icm42688, NULL, icm42688_setup, NULL, NULL, NULL);

ZTEST_F(icm42688, test_fetch_fail_no_ready_data)
{
	uint8_t status = 0;

	icm4268x_emul_set_reg(fixture->target, REG_INT_STATUS, &status, 1);
	zassert_equal(-EBUSY, sensor_sample_fetch(fixture->dev));
}

static void test_fetch_temp_mc(const struct icm42688_fixture *fixture, int16_t temperature_mc)
{
	struct sensor_value value;
	int64_t expected_uc;
	int64_t actual_uc;
	int16_t temperature_reg;
	uint8_t buffer[2];

	/* Set the INT_STATUS register to show we have data */
	buffer[0] = BIT_DATA_RDY_INT;
	icm4268x_emul_set_reg(fixture->target, REG_INT_STATUS, buffer, 1);

	/*
	 * Set the temperature data to 22.5C via:
	 *   reg_val / 132.48 + 25
	 */
	temperature_reg = ((temperature_mc - 25000) * 13248) / 100000;
	buffer[0] = (temperature_reg >> 8) & GENMASK(7, 0);
	buffer[1] = temperature_reg & GENMASK(7, 0);
	icm4268x_emul_set_reg(fixture->target, REG_TEMP_DATA1, buffer, 2);

	/* Fetch the data */
	zassert_ok(sensor_sample_fetch(fixture->dev));
	zassert_ok(sensor_channel_get(fixture->dev, SENSOR_CHAN_DIE_TEMP, &value));

	/* Assert data is within 5 milli-C of the tested temperature */
	expected_uc = temperature_mc * INT64_C(1000);
	actual_uc = sensor_value_to_micro(&value);
	zassert_within(expected_uc, actual_uc, INT64_C(5000),
		       "Expected %" PRIi64 "uC, got %" PRIi64 "uC", expected_uc, actual_uc);
}

ZTEST_F(icm42688, test_fetch_temp)
{
	/* Test 22.5C */
	test_fetch_temp_mc(fixture, 22500);
	/* Test -3.175C */
	test_fetch_temp_mc(fixture, -3175);
}

static void test_fetch_accel_with_range(const struct icm42688_fixture *fixture,
					int16_t accel_range_g, const int16_t accel_percent[3])
{
	struct sensor_value values[3];
	int32_t expect_ug;
	int32_t actual_ug;
	uint8_t register_buffer[6];

	/* Se the INT_STATUS register to show we have data */
	register_buffer[0] = BIT_DATA_RDY_INT;
	icm4268x_emul_set_reg(fixture->target, REG_INT_STATUS, register_buffer, 1);

	/* Set accel range */
	sensor_g_to_ms2(accel_range_g, &values[0]);
	zassert_ok(sensor_attr_set(fixture->dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE,
				   &values[0]));

	/* Set the accel data accel_percent * accel_range_g */
	for (int i = 0; i < 3; ++i) {
		register_buffer[i * 2] = (accel_percent[i] >> 8) & GENMASK(7, 0);
		register_buffer[i * 2 + 1] = accel_percent[i] & GENMASK(7, 0);
	}
	icm4268x_emul_set_reg(fixture->target, REG_ACCEL_DATA_X1, register_buffer, 6);

	/* Fetch the data */
	zassert_ok(sensor_sample_fetch(fixture->dev));
	zassert_ok(sensor_channel_get(fixture->dev, SENSOR_CHAN_ACCEL_XYZ, values));

	/* Assert the data is within 0.005g (0.05m/s2) */
	actual_ug = sensor_ms2_to_ug(&values[0]);
	expect_ug = (int32_t)(accel_percent[0] * INT64_C(1000000) * accel_range_g / INT16_MAX);
	zassert_within(expect_ug, actual_ug, INT32_C(5000),
		       "Expected %" PRIi32 " ug, got X=%" PRIi32 " ug", expect_ug, actual_ug);

	actual_ug = sensor_ms2_to_ug(&values[1]);
	expect_ug = (int32_t)(accel_percent[1] * INT64_C(1000000) * accel_range_g / INT16_MAX);
	zassert_within(expect_ug, actual_ug, INT32_C(5000),
		       "Expected %" PRIi32 " ug, got X=%" PRIi32 " ug", expect_ug, actual_ug);

	actual_ug = sensor_ms2_to_ug(&values[2]);
	expect_ug = (int32_t)(accel_percent[2] * INT64_C(1000000) * accel_range_g / INT16_MAX);
	zassert_within(expect_ug, actual_ug, INT32_C(5000),
		       "Expected %" PRIi32 " ug, got X=%" PRIi32 " ug", expect_ug, actual_ug);
}

ZTEST_F(icm42688, test_fetch_accel)
{
	/* Use (0.25, -0.33.., 0.91) * range for testing accel values */
	const int16_t accel_percent[3] = {
		INT16_MAX / INT16_C(4),
		INT16_MIN / INT16_C(3),
		(int16_t)((INT16_MAX * INT32_C(91)) / INT32_C(100)),
	};

	test_fetch_accel_with_range(fixture, 2, accel_percent);
	test_fetch_accel_with_range(fixture, 4, accel_percent);
	test_fetch_accel_with_range(fixture, 8, accel_percent);
	test_fetch_accel_with_range(fixture, 16, accel_percent);
}

static void test_fetch_gyro_with_range(const struct icm42688_fixture *fixture, int32_t scale_mdps,
				       const int16_t gyro_percent[3])
{
	/* Set the epsilon to 0.075% of the scale */
	const int32_t epsilon_10udps = scale_mdps * 75 / 1000;
	struct sensor_value values[3];
	int32_t expect_10udps;
	int32_t actual_10udps;
	uint8_t register_buffer[6];

	/* Se the INT_STATUS register to show we have data */
	register_buffer[0] = BIT_DATA_RDY_INT;
	icm4268x_emul_set_reg(fixture->target, REG_INT_STATUS, register_buffer, 1);

	/* Set gyro range */
	sensor_degrees_to_rad((scale_mdps / 1000) + (scale_mdps % 1000 == 0 ? 0 : 1), &values[0]);
	zassert_ok(sensor_attr_set(fixture->dev, SENSOR_CHAN_GYRO_XYZ, SENSOR_ATTR_FULL_SCALE,
				   &values[0]));

	/* Set the gyro data to gyro_percent */
	for (int i = 0; i < 3; ++i) {
		register_buffer[i * 2] = (gyro_percent[i] >> 8) & GENMASK(7, 0);
		register_buffer[i * 2 + 1] = gyro_percent[i] & GENMASK(7, 0);
	}
	icm4268x_emul_set_reg(fixture->target, REG_GYRO_DATA_X1, register_buffer, 6);

	/* Fetch the data */
	zassert_ok(sensor_sample_fetch(fixture->dev));
	zassert_ok(sensor_channel_get(fixture->dev, SENSOR_CHAN_GYRO_XYZ, values));

	/* Assert the data is within 0.5 d/s (0.001 rad/s) */
	actual_10udps = sensor_rad_to_10udegrees(&values[0]);
	expect_10udps = (int32_t)(gyro_percent[0] * INT64_C(100) * scale_mdps / INT16_MAX);
	zassert_within(expect_10udps, actual_10udps, epsilon_10udps,
		       "[scale=%" PRIi32 "md/s] Expected %" PRIi32 " 10ud/s, got %" PRIi32
		       " 10ud/s",
		       scale_mdps, expect_10udps, actual_10udps);

	actual_10udps = sensor_rad_to_10udegrees(&values[1]);
	expect_10udps = (int32_t)(gyro_percent[1] * INT64_C(100) * scale_mdps / INT16_MAX);
	zassert_within(expect_10udps, actual_10udps, epsilon_10udps,
		       "[scale=%" PRIi32 "md/s] Expected %" PRIi32 " 10ud/s, got %" PRIi32
		       " 10ud/s, ",
		       scale_mdps, expect_10udps, actual_10udps);

	actual_10udps = sensor_rad_to_10udegrees(&values[2]);
	expect_10udps = (int32_t)(gyro_percent[2] * INT64_C(100) * scale_mdps / INT16_MAX);
	zassert_within(expect_10udps, actual_10udps, epsilon_10udps,
		       "[scale=%" PRIi32 "md/s] Expected %" PRIi32 " 10ud/s, got %" PRIi32
		       " 10ud/s",
		       scale_mdps, expect_10udps, actual_10udps);
}

ZTEST_F(icm42688, test_fetch_gyro)
{
	/* Use (0.15, 0.68, -0.22) * range for testing gyro values */
	const int16_t gyro_percent[3] = {
		(int16_t)((INT16_MAX * INT32_C(15)) / INT32_C(100)),
		(int16_t)((INT16_MAX * INT32_C(68)) / INT32_C(100)),
		(int16_t)((INT16_MAX * INT32_C(-22)) / INT32_C(100)),
	};

	test_fetch_gyro_with_range(fixture, 2000000, gyro_percent);
	test_fetch_gyro_with_range(fixture, 1000000, gyro_percent);
	test_fetch_gyro_with_range(fixture, 500000, gyro_percent);
	test_fetch_gyro_with_range(fixture, 250000, gyro_percent);
	test_fetch_gyro_with_range(fixture, 125000, gyro_percent);
	test_fetch_gyro_with_range(fixture, 62500, gyro_percent);
	test_fetch_gyro_with_range(fixture, 31250, gyro_percent);
	test_fetch_gyro_with_range(fixture, 15625, gyro_percent);
}

FAKE_VOID_FUNC(test_interrupt_trigger_handler, const struct device*, const struct sensor_trigger*);

ZTEST_F(icm42688, test_interrupt)
{
	const struct gpio_dt_spec spec = GPIO_DT_SPEC_GET(NODE, int_gpios);
	const struct sensor_trigger trigger = {
		.type = SENSOR_TRIG_DATA_READY,
		.chan = SENSOR_CHAN_ALL,
	};

	RESET_FAKE(test_interrupt_trigger_handler);
	sensor_trigger_set(fixture->dev, &trigger, test_interrupt_trigger_handler);

	/* Toggle the GPIO */
	gpio_emul_input_set(spec.port, spec.pin, 0);
	k_msleep(5);
	gpio_emul_input_set(spec.port, spec.pin, 1);
	k_msleep(5);

	/* Verify the handler was called */
	zassert_equal(test_interrupt_trigger_handler_fake.call_count, 1);
}

#define FIFO_TEST_FRAMES       6
#define FIFO_TEST_PACKET_SIZE  20
#define FIFO_TEST_TIMESTAMP_NS UINT64_C(5000000000)

struct fifo_test_buffer {
	struct icm4268x_fifo_data hdr;
	uint8_t packets[FIFO_TEST_FRAMES][FIFO_TEST_PACKET_SIZE];
} __aligned(8);

/* 20-bit samples, none equal to the -524288 "invalid" marker */
static int32_t fifo_test_accel(int frame, int axis)
{
	return (axis == 1 ? -1 : 1) * (frame + 1) * (axis + 1) * 11111;
}

static int32_t fifo_test_gyro(int frame, int axis)
{
	return (axis == 2 ? 1 : -1) * (frame + 1) * (axis + 1) * 7777;
}

static int16_t fifo_test_temp(int frame)
{
	return (int16_t)(1000 - frame * 333);
}

static void fifo_test_fill(struct fifo_test_buffer *buf, uint8_t accel_odr, uint8_t gyro_odr,
			   uint16_t rtc_freq)
{
	memset(buf, 0, sizeof(*buf));
	buf->hdr.header.timestamp = FIFO_TEST_TIMESTAMP_NS;
	buf->hdr.header.is_fifo = 1;
	buf->hdr.header.variant = ICM4268X_VARIANT_ICM42688;
	buf->hdr.header.accel_fs = ICM42688_DT_ACCEL_FS_16;
	buf->hdr.header.gyro_fs = ICM42688_DT_GYRO_FS_2000;
	for (int i = 0; i < 3; i++) {
		buf->hdr.header.axis_align[i].index = i;
		buf->hdr.header.axis_align[i].sign = 1;
	}
	buf->hdr.accel_odr = accel_odr;
	buf->hdr.gyro_odr = gyro_odr;
	buf->hdr.fifo_count = sizeof(buf->packets);
	buf->hdr.rtc_freq = rtc_freq;

	for (int f = 0; f < FIFO_TEST_FRAMES; f++) {
		uint8_t *pkt = buf->packets[f];

		pkt[0] = FIFO_HEADER_ACCEL | FIFO_HEADER_GYRO | FIFO_HEADER_20 |
			 FIELD_PREP(FIFO_HEADER_TIMESTAMP_FSYNC, 2);
		for (int a = 0; a < 3; a++) {
			uint32_t accel = (uint32_t)fifo_test_accel(f, a) & GENMASK(19, 0);
			uint32_t gyro = (uint32_t)fifo_test_gyro(f, a) & GENMASK(19, 0);

			sys_put_be16(accel >> 4, &pkt[1 + 2 * a]);
			sys_put_be16(gyro >> 4, &pkt[7 + 2 * a]);
			pkt[17 + a] = ((accel & 0xf) << 4) | (gyro & 0xf);
		}
		sys_put_be16((uint16_t)fifo_test_temp(f), &pkt[13]);
	}
}

/* Time of FIFO frame @p k relative to the first one, ODR scaled by the RTC clock */
static uint64_t fifo_test_delta_ns(uint32_t period_ns, uint16_t rtc_freq, int k)
{
	return (uint64_t)period_ns * k * 32000 / rtc_freq;
}

static void fifo_test_check_q31(const struct sensor_q31_data *data, int frame, uint32_t delta)
{
	/* T = raw / 132.48 + 25 C, Q31 with a shift of 9 */
	int64_t expect_uc = INT64_C(25000000) + fifo_test_temp(frame) * INT64_C(100000000) / 13248;
	int64_t actual_uc = ((int64_t)data->readings[0].temperature * 1000000) >> 22;

	zassert_equal(data->shift, 9);
	zassert_equal(data->readings[0].timestamp_delta, delta, "frame %d", frame);
	zassert_within(expect_uc, actual_uc, 2, "frame %d: expected %" PRIi64 " uC, got %" PRIi64,
		       frame, expect_uc, actual_uc);
}

static void fifo_test_decode_channel(const struct sensor_decoder_api *decoder,
				     const struct fifo_test_buffer *buf, enum sensor_channel chan,
				     uint32_t period_ns)
{
	const struct sensor_chan_spec spec = {.chan_type = chan, .chan_idx = 0};
	const uint8_t *raw = (const uint8_t *)buf;
	const uint16_t rtc_freq = buf->hdr.rtc_freq;
	const uint64_t base = FIFO_TEST_TIMESTAMP_NS -
			      (FIFO_TEST_FRAMES - 1) * fifo_test_delta_ns(period_ns, rtc_freq, 1);
	union {
		struct sensor_three_axis_data three_axis;
		struct sensor_q31_data q31;
		uint8_t raw[sizeof(struct sensor_three_axis_data) +
			    FIFO_TEST_FRAMES * sizeof(struct sensor_three_axis_sample_data)];
	} all, one;
	uint16_t frame_count;
	uint32_t fit = 0;

	zassert_ok(decoder->get_frame_count(raw, spec, &frame_count));
	zassert_equal(frame_count, FIFO_TEST_FRAMES);

	zassert_equal(decoder->decode(raw, spec, &fit, FIFO_TEST_FRAMES, &all), FIFO_TEST_FRAMES);
	zassert_equal(all.three_axis.header.base_timestamp_ns, base,
		      "chan %d rtc %u: base %" PRIu64 ", expected %" PRIu64, chan, rtc_freq,
		      all.three_axis.header.base_timestamp_ns, base);

	/* Decoding one frame per call returns the same samples */
	fit = 0;
	for (int f = 0; f < FIFO_TEST_FRAMES; f++) {
		uint32_t delta = fifo_test_delta_ns(period_ns, rtc_freq, f);

		zassert_equal(decoder->decode(raw, spec, &fit, 1, &one), 1);
		zassert_equal(one.three_axis.header.base_timestamp_ns, base);

		if (chan == SENSOR_CHAN_DIE_TEMP) {
			struct sensor_q31_data *data = &one.q31;

			fifo_test_check_q31(data, f, delta);
			zassert_equal(all.q31.readings[f].timestamp_delta, delta);
			zassert_equal(all.q31.readings[f].temperature,
				      data->readings[0].temperature);
			continue;
		}

		const bool accel = chan == SENSOR_CHAN_ACCEL_XYZ;
		/* LSB of the 20-bit samples in Q31 for +/-16 g and +/-2000 dps */
		const int32_t scale = accel ? 2511 : 2235;
		struct sensor_three_axis_data *data = &one.three_axis;

		zassert_equal(data->shift, accel ? 8 : 6);
		zassert_equal(data->readings[0].timestamp_delta, delta,
			      "chan %d rtc %u frame %d: delta %u, expected %u", chan, rtc_freq, f,
			      data->readings[0].timestamp_delta, delta);
		zassert_equal(all.three_axis.readings[f].timestamp_delta, delta);
		for (int a = 0; a < 3; a++) {
			int32_t sample = accel ? fifo_test_accel(f, a) : fifo_test_gyro(f, a);

			zassert_equal(data->readings[0].values[a], sample * scale,
				      "chan %d frame %d axis %d", chan, f, a);
			zassert_equal(all.three_axis.readings[f].values[a], sample * scale);
		}
	}
	zassert_equal(decoder->decode(raw, spec, &fit, 1, &one), 0);
}

static void fifo_test_decode(const struct device *dev, uint8_t odr, uint32_t period_ns,
			     uint16_t rtc_freq)
{
	const struct sensor_decoder_api *decoder;
	struct fifo_test_buffer buf;

	zassert_ok(sensor_get_decoder(dev, &decoder));
	/* Accel and gyro share the ODR register encoding */
	fifo_test_fill(&buf, odr, odr, rtc_freq);

	fifo_test_decode_channel(decoder, &buf, SENSOR_CHAN_ACCEL_XYZ, period_ns);
	fifo_test_decode_channel(decoder, &buf, SENSOR_CHAN_GYRO_XYZ, period_ns);
	fifo_test_decode_channel(decoder, &buf, SENSOR_CHAN_DIE_TEMP, period_ns);
}

ZTEST_F(icm42688, test_fifo_decode)
{
	/* Internal clock */
	fifo_test_decode(fixture->dev, ICM42688_DT_ACCEL_ODR_1000, 1000000, 32000);
	fifo_test_decode(fixture->dev, ICM42688_DT_ACCEL_ODR_8000, 125000, 32000);
	/* External RTC clock, frame period not a whole number of nanoseconds */
	fifo_test_decode(fixture->dev, ICM42688_DT_ACCEL_ODR_1000, 1000000, 37000);
	fifo_test_decode(fixture->dev, ICM42688_DT_ACCEL_ODR_8000, 125000, 31000);
	fifo_test_decode(fixture->dev, ICM42688_DT_ACCEL_ODR_12_5, 80000000, 50000);
}
