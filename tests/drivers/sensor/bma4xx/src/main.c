/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#include "bma4xx_defs.h"
#include "bma4xx_emul.h"

#define BMA4XX_NODE DT_NODELABEL(bma4xx)

static const struct device *const bma4xx_dev = DEVICE_DT_GET(BMA4XX_NODE);
static const struct emul *const bma4xx_emul = EMUL_DT_GET(BMA4XX_NODE);

SENSOR_DT_STREAM_IODEV(bma4xx_iodev, BMA4XX_NODE,
		       {SENSOR_TRIG_FIFO_WATERMARK, SENSOR_STREAM_DATA_INCLUDE});
RTIO_DEFINE_WITH_MEMPOOL(bma4xx_rtio, 1, 1, 1, 64, sizeof(void *));

/*
 * Register state left behind by bma4xx_chip_init(), sampled once before any
 * test can reconfigure the device.
 */
static uint8_t init_acc_conf;
static uint8_t init_int1_io_ctrl;
static uint8_t init_cmd;

static uint8_t bma4xx_acc_conf(void)
{
	uint8_t val;

	bma4xx_emul_get_reg(bma4xx_emul, BMA4XX_REG_ACCEL_CONFIG, &val, 1);

	return val;
}

/**
 * ACC_CONF holds the output data rate, the bandwidth parameter and the
 * performance power mode bit, so a single write has to carry all three.
 */
ZTEST(bma4xx, test_chip_init_acc_conf)
{
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, init_acc_conf), BMA4XX_ODR_100,
		      "ACC_CONF %#x does not select 100 Hz", init_acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, init_acc_conf), BMA4XX_BWP_NORM_AVG4,
		      "ACC_CONF %#x does not select NORM_AVG4", init_acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, init_acc_conf), 1,
		      "ACC_CONF %#x leaves the sensor in CIC averaging mode", init_acc_conf);
}

/**
 * The INT1 pad is disabled by the soft reset performed at init, so it has to be
 * enabled again for the interrupts mapped to it to reach the host.
 */
ZTEST(bma4xx, test_chip_init_enables_int1_pad)
{
	zassert_equal(init_int1_io_ctrl,
		      BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN | BMA4XX_BIT_INT1_IO_CTRL_LVL,
		      "INT1_IO_CTRL is %#x, expected a push-pull active high output",
		      init_int1_io_ctrl);
}

/**
 * The FIFO is flushed by writing the command value to CMD, not by setting a bit
 * in it.
 */
ZTEST(bma4xx, test_chip_init_flushes_fifo)
{
	zassert_equal(init_cmd, BMA4XX_CMD_FIFO_FLUSH, "last CMD write was %#x, expected %#x",
		      init_cmd, BMA4XX_CMD_FIFO_FLUSH);
}

/** Every bandwidth parameter has to reach the chip unchanged. */
ZTEST(bma4xx, test_attr_set_bwp)
{
	for (uint8_t bwp = BMA4XX_BWP_OSR4_AVG1; bwp <= BMA4XX_BWP_RES_AVG128; bwp++) {
		struct sensor_value val = {.val1 = bwp, .val2 = 0};
		uint8_t acc_conf;

		zassert_ok(sensor_attr_set(bma4xx_dev, SENSOR_CHAN_ACCEL_XYZ,
					   SENSOR_ATTR_CONFIGURATION, &val),
			   "could not set BWP %u", bwp);

		acc_conf = bma4xx_acc_conf();

		zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), bwp,
			      "BWP %u reached the chip as %lu", bwp,
			      FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf));
		zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_100,
			      "BWP %u overwrote the output data rate", bwp);
		zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 1,
			      "BWP %u cleared the performance power mode", bwp);
	}
}

/** Changing the output data rate keeps the rest of ACC_CONF. */
ZTEST(bma4xx, test_attr_set_odr)
{
	struct sensor_value val = {.val1 = 200, .val2 = 0};
	uint8_t acc_conf;

	zassert_ok(sensor_attr_set(bma4xx_dev, SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));

	acc_conf = bma4xx_acc_conf();

	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_200,
		      "ACC_CONF %#x does not select 200 Hz", acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_NORM_AVG4,
		      "the output data rate overwrote the bandwidth parameter");
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 1,
		      "the output data rate cleared the performance power mode");
}

/**
 * Starting a stream maps the watermark interrupt to INT1, which is only of use
 * if the pad driving it is enabled.
 */
ZTEST(bma4xx, test_stream_arms_int1)
{
	struct rtio_sqe *handle = NULL;
	uint8_t int1_io_ctrl;
	uint8_t int_map_data;
	bool latched_mode;

	zassert_ok(sensor_stream(&bma4xx_iodev, &bma4xx_rtio, NULL, &handle));

	/* The stream is set up from the RTIO work queue. */
	k_sleep(K_MSEC(100));

	int_map_data = bma4xx_emul_get_interrupt_config(bma4xx_emul, &int1_io_ctrl, &latched_mode);

	zassert_not_equal(int_map_data & BMA4XX_BIT_INT_MAP_DATA_INT1_FWM, 0,
			  "INT_MAP_DATA is %#x, watermark interrupt not mapped to INT1",
			  int_map_data);
	zassert_equal(int1_io_ctrl,
		      BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN | BMA4XX_BIT_INT1_IO_CTRL_LVL,
		      "INT1_IO_CTRL is %#x, expected a push-pull active high output",
		      int1_io_ctrl);
	zassert_false(latched_mode, "interrupts are latched");

	rtio_sqe_cancel(handle);
}

static void *bma4xx_suite_setup(void)
{
	bool latched_mode;

	zassert_true(device_is_ready(bma4xx_dev), "bma4xx device is not ready");

	bma4xx_emul_get_reg(bma4xx_emul, BMA4XX_REG_ACCEL_CONFIG, &init_acc_conf, 1);
	(void)bma4xx_emul_get_interrupt_config(bma4xx_emul, &init_int1_io_ctrl, &latched_mode);
	init_cmd = bma4xx_emul_get_last_cmd(bma4xx_emul);

	return NULL;
}

static void bma4xx_before(void *fixture)
{
	struct sensor_value val = {.val1 = BMA4XX_BWP_NORM_AVG4, .val2 = 0};

	ARG_UNUSED(fixture);

	zassert_ok(sensor_attr_set(bma4xx_dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
				   &val));

	val.val1 = 100;
	zassert_ok(sensor_attr_set(bma4xx_dev, SENSOR_CHAN_ACCEL_XYZ,
				   SENSOR_ATTR_SAMPLING_FREQUENCY, &val));
}

ZTEST_SUITE(bma4xx, NULL, bma4xx_suite_setup, bma4xx_before, NULL, NULL);
