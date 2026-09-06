/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Tests for the Bosch BMA4xx accelerometer driver
 * @defgroup bma4xx_tests BMA4xx
 * @ingroup all_tests
 * @{
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/emul.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/ztest.h>

#include "bma4xx_decoder.h"
#include "bma4xx_defs.h"
#include "bma4xx_emul.h"

#define BMA4XX_NODE DT_NODELABEL(bma4xx)

static const struct device *const dev = DEVICE_DT_GET(BMA4XX_NODE);
static const struct emul *const target = EMUL_DT_GET(BMA4XX_NODE);

/** Read one register out of the emulated device. */
static uint8_t read_reg(uint8_t reg_addr)
{
	uint8_t value;

	bma4xx_emul_get_reg(target, reg_addr, &value, 1);

	return value;
}

/** Build a sensor_value holding @p ug micro-G expressed in m/s^2. */
static struct sensor_value ug_value(int32_t ug)
{
	struct sensor_value value;

	sensor_ug_to_ms2(ug, &value);

	return value;
}

/**
 * Build a one-shot encoded frame carrying @p x, @p y and @p z as raw 12-bit
 * samples, laid out the way the device returns them: the low nibble in bits 7:4
 * of the first byte of a pair, bits 11:4 in the second.
 */
static void encode_accel(struct bma4xx_encoded_data *edata, uint8_t accel_fs, int16_t x, int16_t y,
			 int16_t z)
{
	const int16_t raw[3] = {x, y, z};

	memset(edata, 0, sizeof(*edata));
	edata->header.accel_fs = accel_fs;

	for (int i = 0; i < 3; i++) {
		edata->accel_xyz_raw_data[i * 2] = (raw[i] & 0x00F) << 4;
		edata->accel_xyz_raw_data[i * 2 + 1] = (raw[i] & 0xFF0) >> 4;
	}
}

static const struct sensor_decoder_api *decoder;

/*
 * Registers as initialization left them. Tests that change the configuration
 * run in an order ztest does not define, so the state initialization produced
 * is captured once, before any of them can overwrite it.
 */
static struct {
	uint8_t acc_conf;
	uint8_t acc_range;
	uint8_t power_ctrl;
	uint8_t power_conf;
	uint8_t int_map_data;
	uint8_t int1_io_ctrl;
	bool latched_mode;
} after_init;

static void *bma4xx_setup(void)
{
	zassert_true(device_is_ready(dev), "%s is not ready", dev->name);
	zassert_not_null(target);
	zassert_ok(sensor_get_decoder(dev, &decoder));
	zassert_not_null(decoder);

	after_init.acc_conf = read_reg(BMA4XX_REG_ACCEL_CONFIG);
	after_init.acc_range = read_reg(BMA4XX_REG_ACCEL_RANGE);
	after_init.power_ctrl = read_reg(BMA4XX_REG_POWER_CTRL);
	after_init.power_conf = read_reg(BMA4XX_REG_POWER_CONF);
	after_init.int_map_data = bma4xx_emul_get_interrupt_config(
		target, &after_init.int1_io_ctrl, &after_init.latched_mode);

	return NULL;
}

ZTEST_SUITE(bma4xx, NULL, bma4xx_setup, NULL, NULL, NULL);

/**
 * @brief Initialization identifies the device and publishes its identifier
 *
 * Reads back SENSOR_ATTR_CHIP_ID after a successful initialization. The
 * emulator answers CHIP_ID with the BMA422 identifier, so a driver that
 * accepted the part must report that same value.
 *
 * @verifies ZEP-SRS-39-1
 * @verifies ZEP-SRS-39-3
 */
ZTEST(bma4xx, test_chip_id)
{
	struct sensor_value value;

	zassert_ok(sensor_attr_get(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CHIP_ID, &value));
	zassert_equal(BMA4XX_CHIP_ID_BMA422, value.val1, "reported chip id %#x", value.val1);
}

/**
 * @brief Initialization leaves the documented default measurement configuration
 *
 * @verifies ZEP-SRS-39-4
 */
ZTEST(bma4xx, test_default_configuration)
{
	zassert_equal(BMA4XX_RANGE_4G, after_init.acc_range & BMA4XX_MASK_ACC_RANGE);
	zassert_equal(BMA4XX_ODR_100, FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, after_init.acc_conf));
	zassert_equal(BMA4XX_BWP_NORM_AVG4,
		      FIELD_GET(BMA4XX_MASK_ACC_CONF_BWP, after_init.acc_conf));
}

/**
 * @brief The accelerometer is enabled and advanced power save is disabled
 *
 * @verifies ZEP-SRS-39-5
 */
ZTEST(bma4xx, test_accelerometer_enabled)
{
	zassert_not_equal(0, after_init.power_ctrl & BMA4XX_BIT_POWER_CTRL_ACC_EN);
	zassert_equal(0, after_init.power_conf & BMA4XX_BIT_POWER_CONF_ADV_PWR_SAVE);
}

/**
 * @brief A sampling frequency is rounded up to a rate the device supports
 *
 * 200 Hz is one of the encodings, and must be programmed exactly. 30 Hz is not,
 * and must select 50 Hz rather than the 25 Hz below it.
 *
 * @verifies ZEP-SRS-39-6
 */
ZTEST(bma4xx, test_odr_rounds_up)
{
	struct sensor_value value = {.val1 = 200};

	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
				   &value));
	zassert_equal(BMA4XX_ODR_200,
		      FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, read_reg(BMA4XX_REG_ACCEL_CONFIG)));

	value = (struct sensor_value){.val1 = 30};
	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SAMPLING_FREQUENCY,
				   &value));
	zassert_equal(BMA4XX_ODR_50,
		      FIELD_GET(BMA4XX_MASK_ACC_CONF_ODR, read_reg(BMA4XX_REG_ACCEL_CONFIG)));
}

/**
 * @brief A sampling frequency the device cannot produce is rejected
 *
 * Zero has no encoding, and the highest rate the part supports is 1600 Hz; both
 * must come back as -ERANGE rather than programming a reserved acc_odr value.
 *
 * @verifies ZEP-SRS-39-6
 */
ZTEST(bma4xx, test_odr_out_of_range)
{
	struct sensor_value value = {.val1 = 0};

	zassert_equal(-ERANGE, sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_SAMPLING_FREQUENCY, &value));

	value = (struct sensor_value){.val1 = 2000};
	zassert_equal(-ERANGE, sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_SAMPLING_FREQUENCY, &value));
}

/**
 * @brief A full-scale range selects the smallest range that covers it
 *
 * 6 g fits in neither +/-2 g nor +/-4 g, so +/-8 g is the smallest range that
 * covers it and must be the one programmed.
 *
 * @verifies ZEP-SRS-39-7
 */
ZTEST(bma4xx, test_full_scale_rounds_up)
{
	struct sensor_value value = ug_value(6000000);

	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &value));
	zassert_equal(BMA4XX_RANGE_8G, read_reg(BMA4XX_REG_ACCEL_RANGE) & BMA4XX_MASK_ACC_RANGE);

	value = ug_value(2000000);
	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &value));
	zassert_equal(BMA4XX_RANGE_2G, read_reg(BMA4XX_REG_ACCEL_RANGE) & BMA4XX_MASK_ACC_RANGE);
}

/**
 * @brief A full-scale range wider than the device supports is rejected
 *
 * @verifies ZEP-SRS-39-7
 */
ZTEST(bma4xx, test_full_scale_out_of_range)
{
	struct sensor_value value = ug_value(20000000);

	zassert_equal(-ERANGE,
		      sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_FULL_SCALE, &value));
}

/**
 * @brief Only the eight documented bandwidth parameters are accepted
 *
 * The field is three bits wide; a value outside osr4_avg1..res_avg128, or a
 * fractional part the parameter has no meaning for, must be refused rather than
 * written into the neighbouring fields of ACC_CONF.
 *
 * @verifies ZEP-SRS-39-8
 */
ZTEST(bma4xx, test_bandwidth_parameter_validation)
{
	struct sensor_value value = {.val1 = BMA4XX_BWP_NORM_AVG4};

	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_CONFIGURATION, &value));

	value = (struct sensor_value){.val1 = BMA4XX_BWP_RES_AVG128 + 1};
	zassert_equal(-EINVAL, sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_CONFIGURATION, &value));

	value = (struct sensor_value){.val1 = -1};
	zassert_equal(-EINVAL, sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_CONFIGURATION, &value));

	value = (struct sensor_value){.val1 = BMA4XX_BWP_NORM_AVG4, .val2 = 1};
	zassert_equal(-EINVAL, sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ,
					       SENSOR_ATTR_CONFIGURATION, &value));
}

/**
 * @brief A per-axis offset is scaled at 3.9 mg per bit
 *
 * 390 mg is exactly 100 offset bits. The m/s^2 round trip can lose one micro-G,
 * so one bit of tolerance is allowed.
 *
 * @verifies ZEP-SRS-39-9
 */
ZTEST(bma4xx, test_offset_within_range)
{
	struct sensor_value value = ug_value(390000);
	uint8_t offset;

	zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_ACCEL_X, SENSOR_ATTR_OFFSET, &value));

	offset = read_reg(BMA4XX_REG_OFFSET_0);
	zassert_between_inclusive(offset, 99, 100, "offset register holds %u", offset);
}

/**
 * @brief An offset the registers cannot express is rejected
 *
 * The offset registers cover +/-0.5 g independently of the full-scale range, so
 * a 1 g offset has no representation.
 *
 * @verifies ZEP-SRS-39-9
 */
ZTEST(bma4xx, test_offset_out_of_range)
{
	struct sensor_value value = ug_value(1000000);

	zassert_equal(-ERANGE,
		      sensor_attr_set(dev, SENSOR_CHAN_ACCEL_X, SENSOR_ATTR_OFFSET, &value));

	value = ug_value(-1000000);
	zassert_equal(-ERANGE,
		      sensor_attr_set(dev, SENSOR_CHAN_ACCEL_X, SENSOR_ATTR_OFFSET, &value));
}

/**
 * @brief An attribute the driver does not implement is refused
 *
 * @verifies ZEP-SRS-39-11
 */
ZTEST(bma4xx, test_unsupported_attribute)
{
	struct sensor_value value = {.val1 = 1};

	zassert_equal(-ENOTSUP,
		      sensor_attr_set(dev, SENSOR_CHAN_ACCEL_XYZ, SENSOR_ATTR_SLOPE_TH, &value));
}

/**
 * @brief A channel the device does not have is refused
 *
 * @verifies ZEP-SRS-39-11
 */
ZTEST(bma4xx, test_unsupported_channel)
{
	struct sensor_value value = {.val1 = 100};

	zassert_equal(-EINVAL, sensor_attr_set(dev, SENSOR_CHAN_GYRO_XYZ,
					       SENSOR_ATTR_SAMPLING_FREQUENCY, &value));
}

/**
 * @brief Acceleration samples are decoded as 12-bit two's complement values
 *
 * 0xFFF is -1 and 0x001 is +1 in the device's encoding. Decoding the register
 * pair without sign-extending from bit 11 turns the first into a large positive
 * reading, which is what this checks for.
 *
 * @verifies ZEP-SRS-39-12
 */
ZTEST(bma4xx, test_decode_sign_extension)
{
	struct bma4xx_encoded_data edata;
	struct sensor_three_axis_data out = {0};
	struct sensor_chan_spec chan = {.chan_type = SENSOR_CHAN_ACCEL_XYZ, .chan_idx = 0};
	uint32_t fit = 0;

	encode_accel(&edata, BMA4XX_RANGE_4G, 0xFFF, 0x001, 0x000);

	zassert_equal(1, decoder->decode((const uint8_t *)&edata, chan, &fit, 1, &out));
	zassert_true(out.readings[0].x < 0, "x decoded as %d", out.readings[0].x);
	zassert_true(out.readings[0].y > 0, "y decoded as %d", out.readings[0].y);
	zassert_equal(0, out.readings[0].z);
}

/**
 * @brief Decoded samples carry the scale of the configured full-scale range
 *
 * The Q31 shift is what turns a raw count into m/s^2, so it has to follow the
 * range the sample was captured at: 6 for +/-4 g, 7 for +/-8 g.
 *
 * @verifies ZEP-SRS-39-12
 */
ZTEST(bma4xx, test_decode_full_scale_shift)
{
	struct bma4xx_encoded_data edata;
	struct sensor_three_axis_data out = {0};
	struct sensor_chan_spec chan = {.chan_type = SENSOR_CHAN_ACCEL_XYZ, .chan_idx = 0};
	uint32_t fit = 0;

	encode_accel(&edata, BMA4XX_RANGE_4G, 0x100, 0, 0);
	zassert_equal(1, decoder->decode((const uint8_t *)&edata, chan, &fit, 1, &out));
	zassert_equal(6, out.shift);

	encode_accel(&edata, BMA4XX_RANGE_8G, 0x100, 0, 0);
	fit = 0;
	zassert_equal(1, decoder->decode((const uint8_t *)&edata, chan, &fit, 1, &out));
	zassert_equal(7, out.shift);
}

/**
 * @brief The die temperature is decoded as an offset from 23 degrees Celsius
 *
 * @verifies ZEP-SRS-39-14
 */
ZTEST(bma4xx, test_decode_die_temperature)
{
	struct bma4xx_encoded_data edata;
	struct sensor_q31_data out = {0};
	struct sensor_chan_spec chan = {.chan_type = SENSOR_CHAN_DIE_TEMP, .chan_idx = 0};
	uint32_t fit = 0;
	int32_t celsius;

	encode_accel(&edata, BMA4XX_RANGE_4G, 0, 0, 0);
	edata.temp = 10;

	zassert_equal(1, decoder->decode((const uint8_t *)&edata, chan, &fit, 1, &out));
	zassert_equal(BMA4XX_TEMP_SHIFT, out.shift);

	celsius = (int32_t)(((int64_t)out.readings[0].temperature << out.shift) /
			    ((int64_t)INT32_MAX + 1));
	zassert_equal(33, celsius, "decoded %d degrees Celsius", celsius);
}

/**
 * @brief The interrupt engine is left in non-latched mode
 *
 * @verifies ZEP-SRS-39-18
 */
ZTEST(bma4xx, test_non_latched_interrupts)
{
	zassert_false(after_init.latched_mode);
}

/**
 * @brief Data ready is the interrupt source when streaming is disabled
 *
 * @verifies ZEP-SRS-39-19
 */
ZTEST(bma4xx, test_data_ready_interrupt_mapped)
{
	zassert_not_equal(0, after_init.int_map_data & BMA4XX_BIT_INT_MAP_DATA_INT1_DRDY);
	zassert_equal(0, after_init.int_map_data & BMA4XX_BIT_INT_MAP_DATA_INT1_FWM);
	zassert_equal(0, after_init.int_map_data & BMA4XX_BIT_INT_MAP_DATA_INT1_FFUL);
}

/**
 * @}
 */
