/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/drivers/sensor/tmp11x.h>
#include <zephyr/ztest.h>
#include "regmap.h"

#define SENSOR_CASE(node) {DEVICE_DT_GET(DT_NODELABEL(node)), EMUL_DT_GET(DT_NODELABEL(node))}
static const struct {
	const struct device *dev;
	const struct emul *emul;
} sensors[] = {
	SENSOR_CASE(lps), SENSOR_CASE(mpu), SENSOR_CASE(p3t), SENSOR_CASE(p3t_one),
	SENSOR_CASE(tcn), SENSOR_CASE(tcn_one), SENSOR_CASE(tmp),
};

ZTEST(sensor_regmap, test_ready)
{
	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		zassert_true(device_is_ready(sensors[i].dev), "%s", sensors[i].dev->name);
	}
}

#if !defined(CONFIG_TEST_SENSOR_REGMAP_BASELINE)
static void check_sample(const struct device *dev, const struct emul *emul,
			 enum sensor_channel channel, double expected)
{
	const struct emul_regmap_config *cfg = emul->cfg;
	struct sensor_chan_spec ch = {.chan_type = channel};
	struct sensor_value result;
	q31_t low, high, epsilon;
	int8_t shift;
	q31_t input;
	double scale = 2147483648.0;

	zassert_ok(emul_sensor_backend_get_sample_range(emul, ch, &low, &high, &epsilon, &shift));
	for (int8_t i = 0; i < shift; i++) {
		scale /= 2;
	}
	input = expected * scale;
	zassert_ok(emul_sensor_backend_set_channel(emul, ch, &input, shift));
	zassert_ok(sensor_sample_fetch(dev), "%s", dev->name);
	zassert_ok(sensor_channel_get(dev, channel, &result));
	zassert_within(sensor_value_to_double(&result), expected, 2.0 * epsilon / scale + 0.000002,
		       "%s channel %d expected %d micro, got %lld micro (%u channels)",
		       dev->name, channel, (int)(expected * 1000000),
		       sensor_value_to_micro(&result), (unsigned int)cfg->channel_count);
}

static void check_sensor(size_t i)
{
	const struct emul_regmap_config *cfg = sensors[i].emul->cfg;

	for (size_t c = 0; c < cfg->channel_count; c++) {
		struct emul_regmap_channel ch = cfg->channels[c];
		double values[] = {-10.5, -0.5, 0, 0.5, 25.5};

		if (cfg->channel != NULL) {
			cfg->channel(sensors[i].emul, &ch);
		}
		for (size_t v = 0; v < ARRAY_SIZE(values); v++) {
			double input = ch.channel == SENSOR_CHAN_PRESS ? 100 + values[v] :
				       CLAMP(values[v], ch.min / 2, ch.max / 2);

			check_sample(sensors[i].dev, sensors[i].emul, ch.channel, input);
		}
	}
}

ZTEST(sensor_regmap, test_lps22hb_samples)
{
	check_sensor(0);
}

ZTEST(sensor_regmap, test_mpu6050_samples)
{
	check_sensor(1);
}

ZTEST(sensor_regmap, test_p3t1755_samples)
{
	check_sensor(2);
	check_sensor(3);
}

ZTEST(sensor_regmap, test_tcn75a_samples)
{
	struct emul_regmap_data *data = sensors[5].emul->data;

	check_sensor(4);
	check_sensor(5);
	zassert_equal(data->values[1] & 1U, 1, "One-shot must return to shutdown");
}

ZTEST(sensor_regmap, test_tmp11x_samples)
{
	check_sensor(6);
}

static int bus_error(const struct emul *target, struct i2c_msg *msgs, int count, int addr)
{
	ARG_UNUSED(target);
	ARG_UNUSED(msgs);
	ARG_UNUSED(count);
	ARG_UNUSED(addr);
	return -EIO;
}

ZTEST(sensor_regmap, test_bus_failure_and_recovery)
{
	static struct i2c_emul_api failing_api = {.transfer = bus_error};

	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		struct i2c_emul *bus = sensors[i].emul->bus.i2c;
		int ret;

		bus->mock_api = &failing_api;
		ret = sensor_sample_fetch(sensors[i].dev);
		bus->mock_api = NULL;
		zassert_true(ret < 0, "%s swallowed bus failure", sensors[i].dev->name);
		check_sample(sensors[i].dev, sensors[i].emul,
			     i == 0U ? SENSOR_CHAN_PRESS :
			     i == 1U ? SENSOR_CHAN_ACCEL_X : SENSOR_CHAN_AMBIENT_TEMP,
			     i == 0U ? 100 : 0);
	}
}

ZTEST(sensor_regmap, test_unsupported_channel)
{
	for (size_t i = 0; i < ARRAY_SIZE(sensors); i++) {
		struct sensor_value value;

		zassert_equal(sensor_channel_get(sensors[i].dev, SENSOR_CHAN_LIGHT, &value),
			      -ENOTSUP);
		if (i >= 2U && i < 6U) {
			zassert_equal(sensor_sample_fetch_chan(sensors[i].dev, SENSOR_CHAN_LIGHT),
				      -ENOTSUP);
		}
	}
}

ZTEST(sensor_regmap, test_tmp11x_attributes)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(tmp));
	struct sensor_value val;
	const int averages[] = {1, 8, 32, 64};

	for (size_t i = 0; i < ARRAY_SIZE(averages); i++) {
		val = (struct sensor_value){.val1 = averages[i]};
		zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_OVERSAMPLING, &val));
		zassert_ok(sensor_attr_get(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_CONFIGURATION, &val));
		zassert_equal((val.val1 >> 5) & 3, i);
	}
	val = (struct sensor_value){.val1 = 3};
	zassert_equal(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP,
				     SENSOR_ATTR_OVERSAMPLING, &val), -EINVAL);
	val = (struct sensor_value){.val1 = -2};
	if (IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116)) {
		zassert_equal(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_OFFSET, &val),
			      -EINVAL);
	} else {
		zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_OFFSET, &val));
		zassert_ok(sensor_attr_get(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_OFFSET, &val));
		zassert_equal(val.val1, -2);
		val.val1 = 0;
		zassert_ok(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP,
					  SENSOR_ATTR_OFFSET, &val));
	}
	zassert_equal(sensor_attr_set(dev, SENSOR_CHAN_LIGHT, SENSOR_ATTR_OFFSET, &val), -ENOTSUP);
	zassert_equal(sensor_attr_get(dev, SENSOR_CHAN_LIGHT, SENSOR_ATTR_OFFSET, &val), -ENOTSUP);
	zassert_equal(sensor_attr_set(dev, SENSOR_CHAN_AMBIENT_TEMP, SENSOR_ATTR_HYSTERESIS, &val),
		      -ENOTSUP);
	zassert_equal(sensor_attr_get(dev, SENSOR_CHAN_AMBIENT_TEMP, SENSOR_ATTR_HYSTERESIS, &val),
		      -ENOTSUP);
}

ZTEST(sensor_regmap, test_tmp11x_eeprom)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(tmp));
	uint16_t written[] = {0xa55a, 0x1234};
	uint16_t readback[2];

	zassert_ok(tmp11x_eeprom_write(dev, 0, written, sizeof(written)));
	zassert_ok(tmp11x_eeprom_read(dev, 0, readback, sizeof(readback)));
	zassert_mem_equal(written, readback, sizeof(written));
	zassert_equal(tmp11x_eeprom_write(dev, 1, written, 2), -EINVAL);
	zassert_equal(tmp11x_eeprom_read(dev, 8, readback, 2), -EINVAL);
	zassert_equal(tmp11x_eeprom_read(dev, 0, readback, 1), -EINVAL);
}
#endif

ZTEST_SUITE(sensor_regmap, NULL, NULL, NULL, NULL, NULL);
