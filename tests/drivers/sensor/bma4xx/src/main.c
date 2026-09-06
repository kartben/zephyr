/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/emul_sensor.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/pm/device.h>
#include <zephyr/rtio/rtio.h>
#include <zephyr/ztest.h>

#include "bma4xx_defs.h"
#include "bma4xx_emul.h"

#define NODE_AH DT_NODELABEL(bma4xx_ah)
#define NODE_AL DT_NODELABEL(bma4xx_al)
#define NODE_LP DT_NODELABEL(bma4xx_lp)

static const struct device *const dev_ah = DEVICE_DT_GET(NODE_AH);
static const struct device *const dev_al = DEVICE_DT_GET(NODE_AL);
static const struct emul *const emul_ah = EMUL_DT_GET(NODE_AH);
static const struct emul *const emul_al = EMUL_DT_GET(NODE_AL);
static const struct device *const dev_lp = DEVICE_DT_GET(NODE_LP);
static const struct emul *const emul_lp = EMUL_DT_GET(NODE_LP);

SENSOR_DT_READ_IODEV(iodev_ah, NODE_AH, {SENSOR_CHAN_ACCEL_XYZ, 0});
RTIO_DEFINE_WITH_MEMPOOL(rtio_ctx, 4, 4, 8, 64, sizeof(void *));

static uint8_t reg_read(const struct emul *target, uint8_t reg)
{
	uint8_t val;

	bma4xx_emul_get_reg(target, reg, &val, 1);

	return val;
}

static int attr_set(const struct device *dev, enum sensor_channel chan, enum sensor_attribute attr,
		    int32_t val1)
{
	struct sensor_value val = {.val1 = val1, .val2 = 0};

	return sensor_attr_set(dev, chan, attr, &val);
}

ZTEST(bma4xx, test_init_config)
{
	uint8_t acc_conf;

	zassert_true(device_is_ready(dev_ah));
	zassert_true(device_is_ready(dev_al));

	acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 1, "ACC_CONF %#x", acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_NORM_AVG4);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_100);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_ACCEL_RANGE), BMA4XX_RANGE_4G);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CTRL) & BMA4XX_BIT_POWER_CTRL_ACC_EN,
		      BMA4XX_BIT_POWER_CTRL_ACC_EN);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CONF) & BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE,
		      0);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_INT_LATCH), 0);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_FIFO_CONFIG_1) &
			      (BMA4XX_FIFO_HEADER_EN | BMA4XX_FIFO_ACC_EN),
		      BMA4XX_FIFO_HEADER_EN | BMA4XX_FIFO_ACC_EN);
}

ZTEST(bma4xx, test_attr_bwp)
{
	uint8_t acc_conf;

	for (int bwp = BMA4XX_BWP_OSR4_AVG1; bwp <= BMA4XX_BWP_NORM_AVG4; bwp++) {
		zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION, bwp));
		acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);
		zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), bwp, "ACC_CONF %#x",
			      acc_conf);
		zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 1);
		zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_100);
	}

	for (int bwp = BMA4XX_BWP_CIC_AVG8; bwp <= BMA4XX_BWP_RES_AVG128; bwp++) {
		zassert_equal(-EINVAL, attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_CONFIGURATION, bwp));
		acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);
		zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_NORM_AVG4);
	}
}

ZTEST(bma4xx, test_attr_odr)
{
	uint8_t acc_conf;

	zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 50));
	acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_50);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_NORM_AVG4);
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 1);

	zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100));
	acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_100);
}

ZTEST(bma4xx, test_fifo_flushed_on_reconfigure)
{
	const uint8_t fifo_length[] = {0x18, 0x01};

	bma4xx_emul_set_reg(emul_ah, BMA4XX_REG_FIFO_LENGTH_0, fifo_length, sizeof(fifo_length));

	zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100));

	zassert_equal(reg_read(emul_ah, BMA4XX_REG_FIFO_LENGTH_0), 0);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_FIFO_LENGTH_1), 0);
}

ZTEST(bma4xx, test_int1_pad_follows_dt_polarity)
{
	uint8_t int1_io_ctrl;
	bool latched;

	bma4xx_emul_get_interrupt_config(emul_ah, &int1_io_ctrl, &latched);
	zassert_false(latched);
	zassert_equal(int1_io_ctrl & BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN,
		      BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN, "INT1_IO_CTRL %#x", int1_io_ctrl);
	zassert_equal(int1_io_ctrl & BMA4XX_BIT_INT1_IO_CTRL_OD, 0);
	zassert_equal(int1_io_ctrl & BMA4XX_BIT_INT1_IO_CTRL_LVL, BMA4XX_BIT_INT1_IO_CTRL_LVL,
		      "active-high int1-gpios must give an active-high INT1");

	bma4xx_emul_get_interrupt_config(emul_al, &int1_io_ctrl, &latched);
	zassert_equal(int1_io_ctrl & BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN,
		      BMA4XX_BIT_INT1_IO_CTRL_OUTPUT_EN, "INT1_IO_CTRL %#x", int1_io_ctrl);
	zassert_equal(int1_io_ctrl & BMA4XX_BIT_INT1_IO_CTRL_LVL, 0,
		      "active-low int1-gpios must give an active-low INT1");
}

ZTEST(bma4xx, test_one_shot_read)
{
	const struct sensor_decoder_api *decoder;
	struct sensor_three_axis_data data;
	struct sensor_chan_spec ch = {SENSOR_CHAN_ACCEL_XYZ, 0};
	const int8_t shift = 6;
	const q31_t one_g = (q31_t)((9.80665 / BIT(shift)) * INT32_MAX);
	const q31_t values[] = {0, 0, one_g};
	uint8_t buf[64];
	uint32_t fit = 0;
	double z;

	zassert_ok(emul_sensor_backend_set_channel(emul_ah, ch, values, shift));
	zassert_ok(sensor_read(&iodev_ah, &rtio_ctx, buf, sizeof(buf)));
	zassert_ok(sensor_get_decoder(dev_ah, &decoder));
	zassert_equal(decoder->decode(buf, ch, &fit, 1, &data), 1);

	z = (double)data.readings[0].z * BIT(data.shift) / INT32_MAX;
	zassert_within(z, 9.80665, 0.05, "z = %f", z);
}

ZTEST(bma4xx, test_low_power_mode_config)
{
	uint8_t acc_conf;

	zassert_true(device_is_ready(dev_lp));

	acc_conf = reg_read(emul_lp, BMA4XX_REG_ACCEL_CONFIG);
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 0, "ACC_CONF %#x", acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_NORM_AVG4);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_100);
	zassert_equal(reg_read(emul_lp, BMA4XX_REG_POWER_CTRL) & BMA4XX_BIT_POWER_CTRL_ACC_EN,
		      BMA4XX_BIT_POWER_CTRL_ACC_EN);
	zassert_equal(reg_read(emul_lp, BMA4XX_REG_POWER_CONF),
		      BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE | BMA4XX_BIT_POWER_CONF_FIFO_SELF_WAKEUP);
}

ZTEST(bma4xx, test_attr_ranges_follow_power_mode)
{
	uint8_t acc_conf;

	/* Performance mode: 12.5 Hz to 1600 Hz, filter settings only */
	zassert_equal(-ERANGE,
		      attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 6));
	zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 1600));
	zassert_ok(attr_set(dev_ah, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100));

	/* Low power mode: 0.78 Hz to 400 Hz, 2^BWP averaged samples must fit in the ODR period */
	zassert_equal(-ERANGE,
		      attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 800));
	zassert_ok(attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
			    BMA4XX_BWP_CIC_AVG8));
	zassert_equal(-EINVAL, attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
					BMA4XX_BWP_RES_AVG16));
	zassert_ok(attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 1));
	zassert_ok(attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
			    BMA4XX_BWP_RES_AVG128));
	acc_conf = reg_read(emul_lp, BMA4XX_REG_ACCEL_CONFIG);
	zassert_equal(FIELD_GET(BMA4XX_BIT_ACC_PERF_MODE, acc_conf), 0, "ACC_CONF %#x", acc_conf);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, acc_conf), BMA4XX_BWP_RES_AVG128);
	zassert_equal(FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, acc_conf), BMA4XX_ODR_1_5625);
	zassert_equal(-EINVAL, attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
					BMA4XX_BWP_RES_AVG128 + 1));
	zassert_equal(-EINVAL,
		      attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100));

	zassert_ok(attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION,
			    BMA4XX_BWP_NORM_AVG4));
	zassert_ok(attr_set(dev_lp, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY, 100));
}

ZTEST(bma4xx, test_pm_suspend_resume)
{
	const uint8_t acc_conf = reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG);

	zassert_ok(pm_device_action_run(dev_ah, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CTRL) & BMA4XX_BIT_POWER_CTRL_ACC_EN, 0);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CONF) & BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE,
		      BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE);

	zassert_ok(pm_device_action_run(dev_ah, PM_DEVICE_ACTION_RESUME));
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CTRL) & BMA4XX_BIT_POWER_CTRL_ACC_EN,
		      BMA4XX_BIT_POWER_CTRL_ACC_EN);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_POWER_CONF) & BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE,
		      0);
	zassert_equal(reg_read(emul_ah, BMA4XX_REG_ACCEL_CONFIG), acc_conf);

	/* Low power mode re-enters advanced power save on resume */
	zassert_ok(pm_device_action_run(dev_lp, PM_DEVICE_ACTION_SUSPEND));
	zassert_equal(reg_read(emul_lp, BMA4XX_REG_POWER_CTRL) & BMA4XX_BIT_POWER_CTRL_ACC_EN, 0);
	zassert_ok(pm_device_action_run(dev_lp, PM_DEVICE_ACTION_RESUME));
	zassert_equal(reg_read(emul_lp, BMA4XX_REG_POWER_CONF),
		      BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE | BMA4XX_BIT_POWER_CONF_FIFO_SELF_WAKEUP);
}

ZTEST_SUITE(bma4xx, NULL, NULL, NULL, NULL, NULL);
