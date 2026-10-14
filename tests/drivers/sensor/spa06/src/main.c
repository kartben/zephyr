/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/ztest.h>

#include "spa06_emul.h"

#define SPA06_I2C DT_NODELABEL(spa06_i2c)
#define SPA06_SPI DT_NODELABEL(spa06_spi)

/* Scale factors for 1x, 16x and 64x oversampling */
#define KX1  524288.0
#define KX16 253952.0
#define KX64 1040384.0

#define PRESS_TOL_KPA 0.0001
#define TEMP_TOL_C    0.0001

static const struct spa06_coef test_coef = {
	.c0 = 100,
	.c1 = -125,
	.c00 = 80000,
	.c10 = -54630,
	.c01 = -2108,
	.c11 = 1206,
	.c20 = -11683,
	.c21 = 252,
	.c30 = -1556,
	.c31 = -40,
	.c40 = 85,
};

/* About 99 kPa and 25 degrees Celsius with 16x pressure and 1x temperature oversampling */
#define RAW_PRS -88883
#define RAW_TMP 104858

static const struct gpio_dt_spec int_gpio = GPIO_DT_SPEC_GET(SPA06_I2C, int_gpios);

static struct sensor_trigger drdy_trig = {
	.type = SENSOR_TRIG_DATA_READY,
	.chan = SENSOR_CHAN_ALL,
};

SENSOR_DT_READ_IODEV(spa06_read_iodev, SPA06_I2C, {SENSOR_CHAN_PRESS, 0},
		     {SENSOR_CHAN_AMBIENT_TEMP, 0});
RTIO_DEFINE(spa06_read_rtio, 1, 1);

SENSOR_DT_STREAM_IODEV(spa06_drdy_iodev, SPA06_I2C,
		       {SENSOR_TRIG_DATA_READY, SENSOR_STREAM_DATA_INCLUDE});
SENSOR_DT_STREAM_IODEV(spa06_fifo_iodev, SPA06_I2C,
		       {SENSOR_TRIG_FIFO_FULL, SENSOR_STREAM_DATA_INCLUDE});
RTIO_DEFINE_WITH_MEMPOOL(spa06_stream_rtio, 4, 4, 32, 64, 4);

struct spa06_fixture {
	const struct device *i2c_dev;
	const struct emul *i2c_emul;
	const struct device *spi_dev;
	const struct emul *spi_emul;
	/* Running stream, stopped after the test if it failed before doing so */
	struct rtio_sqe *stream;
};

static double temp_c(int32_t raw_tmp, double kt)
{
	return (test_coef.c0 * 0.5) + (test_coef.c1 * (raw_tmp / kt));
}

static double press_kpa(int32_t raw_prs, double kp, int32_t raw_tmp, double kt)
{
	const struct spa06_coef *c = &test_coef;
	double p = raw_prs / kp;
	double t = raw_tmp / kt;
	double pa = c->c00 + (p * (c->c10 + (p * (c->c20 + (p * (c->c30 + (p * c->c40))))))) +
		    (t * (c->c01 + (p * (c->c11 + (p * (c->c21 + (p * c->c31)))))));

	return pa / 1000.0;
}

static double q31_to_double(int32_t value, int8_t shift)
{
	return ldexp((double)value, shift - 31);
}

static void check_values(const struct device *dev, double exp_press, double exp_temp)
{
	struct sensor_value val;

	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_PRESS, &val));
	zassert_within(sensor_value_to_double(&val), exp_press, PRESS_TOL_KPA,
		       "pressure %f kPa, expected %f", sensor_value_to_double(&val), exp_press);

	zassert_ok(sensor_channel_get(dev, SENSOR_CHAN_AMBIENT_TEMP, &val));
	zassert_within(sensor_value_to_double(&val), exp_temp, TEMP_TOL_C,
		       "temperature %f C, expected %f", sensor_value_to_double(&val), exp_temp);
}

static void set_odr(const struct device *dev, enum sensor_channel chan, int32_t val1, int32_t val2)
{
	struct sensor_value odr = {.val1 = val1, .val2 = val2};

	zassert_ok(sensor_attr_set(dev, chan, SENSOR_ATTR_SAMPLING_FREQUENCY, &odr));
}

static void set_osr(const struct device *dev, enum sensor_channel chan, int32_t osr)
{
	struct sensor_value val = {.val1 = osr};

	zassert_ok(sensor_attr_set(dev, chan, SENSOR_ATTR_OVERSAMPLING, &val));
}

static void raise_interrupt(const struct emul *target, uint8_t int_sts)
{
	spa06_emul_set_int_status(target, int_sts);

	/* The interrupt is active low */
	zassert_ok(gpio_emul_input_set(int_gpio.port, int_gpio.pin, 0));
	k_msleep(20);
	zassert_ok(gpio_emul_input_set(int_gpio.port, int_gpio.pin, 1));
}

static struct rtio_cqe *wait_cqe(struct rtio *ctx)
{
	for (int i = 0; i < 100; i++) {
		struct rtio_cqe *cqe = rtio_cqe_consume(ctx);

		if (cqe != NULL) {
			return cqe;
		}
		k_msleep(10);
	}

	return NULL;
}

static void *spa06_setup(void)
{
	static struct spa06_fixture fixture = {
		.i2c_dev = DEVICE_DT_GET(SPA06_I2C),
		.i2c_emul = EMUL_DT_GET(SPA06_I2C),
		.spi_dev = DEVICE_DT_GET(SPA06_SPI),
		.spi_emul = EMUL_DT_GET(SPA06_SPI),
	};

	spa06_emul_set_coef(fixture.i2c_emul, &test_coef);
	spa06_emul_set_coef(fixture.spi_emul, &test_coef);

	zassert_ok(device_init(fixture.i2c_dev));
	zassert_ok(device_init(fixture.spi_dev));

	/* The driver configured the interrupt pin, which idles high */
	zassert_ok(gpio_emul_input_set(int_gpio.port, int_gpio.pin, 1));

	return &fixture;
}

/* Bring a device back to the active state, after a test left it suspended or off */
static void resume_device(const struct device *dev)
{
	enum pm_device_state state;

	if (!IS_ENABLED(CONFIG_PM_DEVICE)) {
		return;
	}

	zassert_ok(pm_device_state_get(dev, &state));

	if (state == PM_DEVICE_STATE_OFF) {
		zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_TURN_ON));
		state = PM_DEVICE_STATE_SUSPENDED;
	}

	if (state == PM_DEVICE_STATE_SUSPENDED) {
		zassert_ok(pm_device_action_run(dev, PM_DEVICE_ACTION_RESUME));
	}
}

static void spa06_before(void *f)
{
	struct spa06_fixture *fixture = f;
	const struct device *devs[] = {fixture->i2c_dev, fixture->spi_dev};

	ARRAY_FOR_EACH(devs, i) {
		resume_device(devs[i]);
	}

	(void)sensor_trigger_set(fixture->i2c_dev, &drdy_trig, NULL);

	ARRAY_FOR_EACH(devs, i) {
		set_odr(devs[i], SENSOR_CHAN_PRESS, 0, 0);
		set_odr(devs[i], SENSOR_CHAN_AMBIENT_TEMP, 1, 0);
		set_osr(devs[i], SENSOR_CHAN_PRESS, 16);
		set_osr(devs[i], SENSOR_CHAN_AMBIENT_TEMP, 1);
	}

	spa06_emul_set_raw(fixture->i2c_emul, RAW_PRS, RAW_TMP);
	spa06_emul_set_raw(fixture->spi_emul, RAW_PRS, RAW_TMP);
}

static void stop_stream(struct spa06_fixture *fixture);

static void spa06_after(void *f)
{
	struct spa06_fixture *fixture = f;

	if (fixture->stream != NULL) {
		stop_stream(fixture);
	}
}

ZTEST_SUITE(spa06, NULL, spa06_setup, spa06_before, spa06_after, NULL);

ZTEST_F(spa06, test_init_configuration)
{
	zassert_true(device_is_ready(fixture->i2c_dev));
	zassert_true(device_is_ready(fixture->spi_dev));

	/* 16x pressure oversampling needs the result shift; active-low interrupt */
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x04);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_TMP_CFG), 0x00);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_MEAS_CFG) & 0x07, 0);
}

ZTEST_F(spa06, test_fetch_i2c)
{
	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
	check_values(fixture->i2c_dev, press_kpa(RAW_PRS, KX16, RAW_TMP, KX1),
		     temp_c(RAW_TMP, KX1));
}

ZTEST_F(spa06, test_fetch_spi)
{
	zassert_ok(sensor_sample_fetch(fixture->spi_dev));
	check_values(fixture->spi_dev, press_kpa(RAW_PRS, KX16, RAW_TMP, KX1),
		     temp_c(RAW_TMP, KX1));
}

ZTEST_F(spa06, test_fetch_temperature_only)
{
	struct sensor_value val;

	zassert_ok(sensor_sample_fetch_chan(fixture->i2c_dev, SENSOR_CHAN_AMBIENT_TEMP));
	zassert_ok(sensor_channel_get(fixture->i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, &val));
	zassert_within(sensor_value_to_double(&val), temp_c(RAW_TMP, KX1), TEMP_TOL_C);

	zassert_equal(sensor_sample_fetch_chan(fixture->i2c_dev, SENSOR_CHAN_ACCEL_XYZ), -ENOTSUP);
}

ZTEST_F(spa06, test_oversampling)
{
	struct sensor_value val = {.val1 = 3};
	const int32_t raw_prs = -363946;

	zassert_equal(sensor_attr_set(fixture->i2c_dev, SENSOR_CHAN_PRESS, SENSOR_ATTR_OVERSAMPLING,
				      &val),
		      -EINVAL);
	val.val1 = 256;
	zassert_equal(sensor_attr_set(fixture->i2c_dev, SENSOR_CHAN_PRESS, SENSOR_ATTR_OVERSAMPLING,
				      &val),
		      -EINVAL);

	set_osr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 64);
	set_osr(fixture->i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, 2);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x06);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_TMP_CFG), 0x01);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT);

	zassert_ok(sensor_attr_get(fixture->i2c_dev, SENSOR_CHAN_PRESS, SENSOR_ATTR_OVERSAMPLING,
				   &val));
	zassert_equal(val.val1, 64);

	/* The result shift follows the oversampling */
	set_osr(fixture->i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, 32);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT | SPA06_CFG_REG_T_SHIFT);
	set_osr(fixture->i2c_dev, SENSOR_CHAN_AMBIENT_TEMP, 1);

	spa06_emul_set_raw(fixture->i2c_emul, raw_prs, RAW_TMP);
	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
	check_values(fixture->i2c_dev, press_kpa(raw_prs, KX64, RAW_TMP, KX1),
		     temp_c(RAW_TMP, KX1));
}

ZTEST_F(spa06, test_sampling_frequency)
{
	struct sensor_value val;

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 16, 0);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x44);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_MEAS_CFG) & 0x07,
		      SPA06_MEAS_CTRL_CONTINUOUS);

	/* Requests round up to the next supported rate */
	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 20, 0);
	zassert_ok(sensor_attr_get(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));
	zassert_equal(val.val1, 25);
	zassert_equal(val.val2, 0);

	/* 1.5625 Hz exists for pressure only */
	set_odr(fixture->i2c_dev, SENSOR_CHAN_ALL, 1, 500000);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG) >> 4, 0x8);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_TMP_CFG) >> 4, 0x1);
	zassert_ok(sensor_attr_get(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));
	zassert_equal(val.val1, 1);
	zassert_equal(val.val2, 562500);

	val = (struct sensor_value){.val1 = 250};
	zassert_equal(sensor_attr_set(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				      SENSOR_ATTR_SAMPLING_FREQUENCY, &val),
		      -EINVAL);

	/* 128x pressure oversampling takes 206.8 ms, too long for 16 Hz */
	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 16, 0);
	val = (struct sensor_value){.val1 = 128};
	zassert_equal(sensor_attr_set(fixture->i2c_dev, SENSOR_CHAN_PRESS, SENSOR_ATTR_OVERSAMPLING,
				      &val),
		      -EINVAL);

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 0, 0);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_MEAS_CFG) & 0x07,
		      SPA06_MEAS_CTRL_IDLE);
	zassert_ok(sensor_attr_get(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));
	zassert_equal(val.val1, 0);
}

ZTEST_F(spa06, test_continuous_fetch)
{
	const int32_t raw_prs = -80000;
	const int32_t raw_tmp = 110000;

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 8, 0);
	spa06_emul_set_raw(fixture->i2c_emul, raw_prs, raw_tmp);

	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
	check_values(fixture->i2c_dev, press_kpa(raw_prs, KX16, raw_tmp, KX1),
		     temp_c(raw_tmp, KX1));
}

static atomic_t drdy_count;

static void drdy_handler(const struct device *dev, const struct sensor_trigger *trig)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(trig);

	atomic_inc(&drdy_count);
}

ZTEST_F(spa06, test_trigger_needs_continuous_mode)
{
	zassert_equal(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, drdy_handler), -EINVAL);
}

ZTEST_F(spa06, test_trigger_data_ready)
{
	struct sensor_value zero = {0};

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 8, 0);
	atomic_set(&drdy_count, 0);

	zassert_ok(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, drdy_handler));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_INT_PRS | SPA06_CFG_REG_P_SHIFT);

	/* On-demand measurements would stop the interrupts */
	zassert_equal(sensor_attr_set(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				      SENSOR_ATTR_SAMPLING_FREQUENCY, &zero),
		      -EBUSY);

	raise_interrupt(fixture->i2c_emul, SPA06_INT_STS_PRS);
	zassert_equal(atomic_get(&drdy_count), 1);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_INT_STS), 0);

	/* An interrupt for another source does not call the handler */
	raise_interrupt(fixture->i2c_emul, 0);
	zassert_equal(atomic_get(&drdy_count), 1);

	zassert_ok(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, NULL));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT);
}

ZTEST_F(spa06, test_trigger_not_on_spi)
{
	set_odr(fixture->spi_dev, SENSOR_CHAN_PRESS, 8, 0);
	zassert_equal(sensor_trigger_set(fixture->spi_dev, &drdy_trig, drdy_handler), -ENOTSUP);
}

ZTEST_F(spa06, test_read_decode)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_q31_data data;
	uint8_t buf[64];
	uint32_t fit = 0;
	uint16_t count;

	zassert_ok(sensor_read(&spa06_read_iodev, &spa06_read_rtio, buf, sizeof(buf)));
	zassert_ok(sensor_get_decoder(fixture->i2c_dev, &decoder));

	zassert_ok(decoder->get_frame_count(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0},
					    &count));
	zassert_equal(count, 1);
	zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0}, &fit, 1,
				      &data),
		      1);
	zassert_within(q31_to_double(data.readings[0].pressure, data.shift),
		       press_kpa(RAW_PRS, KX16, RAW_TMP, KX1), PRESS_TOL_KPA);

	fit = 0;
	zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0},
				      &fit, 1, &data),
		      1);
	zassert_within(q31_to_double(data.readings[0].temperature, data.shift),
		       temp_c(RAW_TMP, KX1), TEMP_TOL_C);

	zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0},
				      &fit, 1, &data),
		      0);
	zassert_equal(decoder->get_frame_count(buf, (struct sensor_chan_spec){SENSOR_CHAN_LIGHT, 0},
					       &count),
		      -ENOTSUP);
}

static void stop_stream(struct spa06_fixture *fixture)
{
	struct rtio_cqe *cqe;

	zassert_ok(rtio_sqe_cancel(fixture->stream));
	fixture->stream = NULL;

	/* The driver completes the canceled request on the next interrupt */
	raise_interrupt(fixture->i2c_emul, 0);
	while ((cqe = rtio_cqe_consume(&spa06_stream_rtio)) != NULL) {
		rtio_cqe_release(&spa06_stream_rtio, cqe);
	}
}

ZTEST_F(spa06, test_stream_data_ready)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_q31_data data;
	struct rtio_sqe *handle;
	struct rtio_cqe *cqe;
	uint32_t buf_len;
	uint32_t fit = 0;
	uint8_t *buf;

	zassert_ok(sensor_get_decoder(fixture->i2c_dev, &decoder));

	/* Streaming needs continuous measurements */
	zassert_ok(sensor_stream(&spa06_drdy_iodev, &spa06_stream_rtio, NULL, &handle));
	cqe = wait_cqe(&spa06_stream_rtio);
	zassert_not_null(cqe);
	zassert_equal(cqe->result, -EINVAL);
	rtio_cqe_release(&spa06_stream_rtio, cqe);

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 8, 0);
	zassert_ok(sensor_stream(&spa06_drdy_iodev, &spa06_stream_rtio, NULL, &fixture->stream));
	k_msleep(10);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_INT_PRS | SPA06_CFG_REG_P_SHIFT);
	zassert_equal(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, drdy_handler), -EBUSY);

	raise_interrupt(fixture->i2c_emul, SPA06_INT_STS_PRS);
	cqe = wait_cqe(&spa06_stream_rtio);
	zassert_not_null(cqe);
	zassert_ok(cqe->result);
	zassert_ok(rtio_cqe_get_mempool_buffer(&spa06_stream_rtio, cqe, &buf, &buf_len));
	rtio_cqe_release(&spa06_stream_rtio, cqe);

	zassert_true(decoder->has_trigger(buf, SENSOR_TRIG_DATA_READY));
	zassert_false(decoder->has_trigger(buf, SENSOR_TRIG_FIFO_FULL));
	zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0}, &fit, 1,
				      &data),
		      1);
	zassert_within(q31_to_double(data.readings[0].pressure, data.shift),
		       press_kpa(RAW_PRS, KX16, RAW_TMP, KX1), PRESS_TOL_KPA);
	rtio_release_buffer(&spa06_stream_rtio, buf, buf_len);

	stop_stream(fixture);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT);
}

ZTEST_F(spa06, test_stream_fifo)
{
	const int32_t raw_prs[] = {-88001, -88501, -89001};
	const int32_t raw_tmp[] = {104000, 106000};
	const struct sensor_decoder_api *decoder;
	struct sensor_q31_data data;
	uint64_t prs_ts[3];
	double prs[3];
	double tmp[2];
	struct rtio_cqe *cqe;
	uint32_t buf_len;
	uint32_t fit = 0;
	uint16_t count;
	uint8_t *buf;

	zassert_ok(sensor_get_decoder(fixture->i2c_dev, &decoder));

	set_odr(fixture->i2c_dev, SENSOR_CHAN_ALL, 8, 0);
	zassert_ok(sensor_stream(&spa06_fifo_iodev, &spa06_stream_rtio, NULL, &fixture->stream));
	k_msleep(10);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_FIFO_EN | SPA06_CFG_REG_INT_FIFO | SPA06_CFG_REG_P_SHIFT);

	/* The result registers return FIFO entries, so fetching is refused */
	zassert_equal(sensor_sample_fetch(fixture->i2c_dev), -EBUSY);

	/* Pressure entries have bit 0 set, temperature entries have it clear */
	zassert_ok(spa06_emul_fifo_push(fixture->i2c_emul, (uint32_t)raw_prs[0] & 0xFFFFFF));
	zassert_ok(spa06_emul_fifo_push(fixture->i2c_emul, (uint32_t)raw_tmp[0] & 0xFFFFFF));
	zassert_ok(spa06_emul_fifo_push(fixture->i2c_emul, (uint32_t)raw_prs[1] & 0xFFFFFF));
	zassert_ok(spa06_emul_fifo_push(fixture->i2c_emul, (uint32_t)raw_tmp[1] & 0xFFFFFF));
	zassert_ok(spa06_emul_fifo_push(fixture->i2c_emul, (uint32_t)raw_prs[2] & 0xFFFFFF));

	raise_interrupt(fixture->i2c_emul, SPA06_INT_STS_FIFO_FULL);
	cqe = wait_cqe(&spa06_stream_rtio);
	zassert_not_null(cqe);
	zassert_ok(cqe->result);
	zassert_ok(rtio_cqe_get_mempool_buffer(&spa06_stream_rtio, cqe, &buf, &buf_len));
	rtio_cqe_release(&spa06_stream_rtio, cqe);

	zassert_true(decoder->has_trigger(buf, SENSOR_TRIG_FIFO_FULL));
	zassert_ok(decoder->get_frame_count(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0},
					    &count));
	zassert_equal(count, 3);
	zassert_ok(decoder->get_frame_count(
		buf, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0}, &count));
	zassert_equal(count, 2);

	ARRAY_FOR_EACH(prs, i) {
		zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0},
					      &fit, 1, &data),
			      1);
		prs[i] = q31_to_double(data.readings[0].pressure, data.shift);
		prs_ts[i] = data.header.base_timestamp_ns + data.readings[0].timestamp_delta;
	}
	zassert_equal(decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_PRESS, 0}, &fit, 1,
				      &data),
		      0);

	fit = 0;
	ARRAY_FOR_EACH(tmp, i) {
		zassert_equal(
			decoder->decode(buf, (struct sensor_chan_spec){SENSOR_CHAN_AMBIENT_TEMP, 0},
					&fit, 1, &data),
			1);
		tmp[i] = q31_to_double(data.readings[0].temperature, data.shift);
	}
	rtio_release_buffer(&spa06_stream_rtio, buf, buf_len);

	/* Samples are one pressure period (8 Hz) apart */
	zassert_equal(prs_ts[1] - prs_ts[0], 125000000);
	zassert_equal(prs_ts[2] - prs_ts[1], 125000000);

	/* Each pressure entry is compensated with the temperature before it, or the first one */
	zassert_within(prs[0], press_kpa(raw_prs[0], KX16, raw_tmp[0], KX1), PRESS_TOL_KPA);
	zassert_within(prs[1], press_kpa(raw_prs[1], KX16, raw_tmp[0], KX1), PRESS_TOL_KPA);
	zassert_within(prs[2], press_kpa(raw_prs[2], KX16, raw_tmp[1], KX1), PRESS_TOL_KPA);
	zassert_within(tmp[0], temp_c(raw_tmp[0], KX1), TEMP_TOL_C);
	zassert_within(tmp[1], temp_c(raw_tmp[1], KX1), TEMP_TOL_C);

	stop_stream(fixture);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_P_SHIFT);

	/* With the FIFO disabled, fetching returns the latest results again */
	spa06_emul_set_raw(fixture->i2c_emul, RAW_PRS, RAW_TMP);
	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
	check_values(fixture->i2c_dev, press_kpa(RAW_PRS, KX16, RAW_TMP, KX1),
		     temp_c(RAW_TMP, KX1));
}

ZTEST_F(spa06, test_pm_suspend_resume)
{
	struct sensor_value val;

	Z_TEST_SKIP_IFNDEF(CONFIG_PM_DEVICE);

	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 8, 0);
	atomic_set(&drdy_count, 0);
	zassert_ok(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, drdy_handler));

	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_MEAS_CFG) & 0x07,
		      SPA06_MEAS_CTRL_IDLE);
	zassert_equal(sensor_sample_fetch(fixture->i2c_dev), -EBUSY);

	/* The interrupt is masked while suspended */
	raise_interrupt(fixture->i2c_emul, SPA06_INT_STS_PRS);
	zassert_equal(atomic_get(&drdy_count), 0);

	/* Settings changed while suspended are applied on resume */
	set_odr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 16, 0);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x34);
	zassert_ok(sensor_attr_get(fixture->i2c_dev, SENSOR_CHAN_PRESS,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));
	zassert_equal(val.val1, 16);

	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x44);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_MEAS_CFG) & 0x07,
		      SPA06_MEAS_CTRL_CONTINUOUS);
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_CFG_REG),
		      SPA06_CFG_REG_INT_PRS | SPA06_CFG_REG_P_SHIFT);

	raise_interrupt(fixture->i2c_emul, SPA06_INT_STS_PRS);
	zassert_equal(atomic_get(&drdy_count), 1);

	zassert_ok(sensor_trigger_set(fixture->i2c_dev, &drdy_trig, NULL));
	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
}

ZTEST_F(spa06, test_pm_power_cycle)
{
	const int32_t raw_prs = -363946;

	Z_TEST_SKIP_IFNDEF(CONFIG_PM_DEVICE);

	set_osr(fixture->i2c_dev, SENSOR_CHAN_PRESS, 64);
	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_SUSPEND));
	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_TURN_OFF));

	/* Turning on resets the sensor, resuming restores the configuration */
	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_TURN_ON));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x00);
	zassert_ok(pm_device_action_run(fixture->i2c_dev, PM_DEVICE_ACTION_RESUME));
	zassert_equal(spa06_emul_get_reg(fixture->i2c_emul, SPA06_REG_PRS_CFG), 0x06);

	spa06_emul_set_raw(fixture->i2c_emul, raw_prs, RAW_TMP);
	zassert_ok(sensor_sample_fetch(fixture->i2c_dev));
	check_values(fixture->i2c_dev, press_kpa(raw_prs, KX64, RAW_TMP, KX1),
		     temp_c(RAW_TMP, KX1));
}
