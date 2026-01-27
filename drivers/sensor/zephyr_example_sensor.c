/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Example sensor driver demonstrating the use of sensor driver helper macros
 *
 * This is a reference implementation showing how to use the sensor driver helper
 * macros to reduce boilerplate code while maintaining readability.
 *
 * This example demonstrates:
 * - I2C register access helpers
 * - Device initialization helpers
 * - Channel dispatch helpers
 * - Power management helpers
 * - Device instantiation helpers
 */

#define DT_DRV_COMPAT zephyr_example_sensor

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/sensor/sensor_driver_helpers.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(EXAMPLE_SENSOR, CONFIG_SENSOR_LOG_LEVEL);

/* Register definitions for hypothetical sensor */
#define EXAMPLE_REG_TEMP		0x00
#define EXAMPLE_REG_HUMIDITY		0x01
#define EXAMPLE_REG_CONFIG		0x02
#define EXAMPLE_REG_STATUS		0x03

/* Config register bits */
#define EXAMPLE_CONFIG_ENABLE		BIT(0)
#define EXAMPLE_CONFIG_CONTINUOUS	BIT(1)

/**
 * Driver data structure
 *
 * Stores runtime state including cached sensor readings.
 */
struct example_sensor_data {
	struct sensor_value temp;
	struct sensor_value humidity;
};

/**
 * Driver config structure
 *
 * Stores compile-time configuration from device tree.
 */
struct example_sensor_config {
	struct i2c_dt_spec i2c;
};

/*
 * Generate register access helper functions:
 * - example_sensor_reg_read()
 * - example_sensor_reg_write()
 *
 * These inline functions automatically extract the I2C spec from the
 * device config and perform register read/write operations.
 */
SENSOR_I2C_REG_ACCESS(example_sensor, example_sensor_config, i2c)

/**
 * Fetch temperature reading from sensor
 *
 * Reads the raw temperature register and converts it to a sensor_value.
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative errno on failure
 */
static int example_sensor_fetch_temp(const struct device *dev)
{
	struct example_sensor_data *data = dev->data;
	uint8_t raw_temp;
	int ret;

	ret = example_sensor_reg_read(dev, EXAMPLE_REG_TEMP, &raw_temp, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read temperature register");
		return ret;
	}

	/* Convert raw value to temperature in degrees Celsius
	 * This is a simplified example conversion
	 */
	data->temp.val1 = (int32_t)raw_temp - 40; /* Offset of -40°C */
	data->temp.val2 = 0;

	return 0;
}

/**
 * Fetch humidity reading from sensor
 *
 * Reads the raw humidity register and converts it to a sensor_value.
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative errno on failure
 */
static int example_sensor_fetch_humidity(const struct device *dev)
{
	struct example_sensor_data *data = dev->data;
	uint8_t raw_humidity;
	int ret;

	ret = example_sensor_reg_read(dev, EXAMPLE_REG_HUMIDITY, &raw_humidity, 1);
	if (ret < 0) {
		LOG_ERR("Failed to read humidity register");
		return ret;
	}

	/* Convert raw value to relative humidity percentage
	 * This is a simplified example conversion
	 */
	data->humidity.val1 = raw_humidity;
	data->humidity.val2 = 0;

	return 0;
}

/**
 * Fetch all sensor readings
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative errno on failure
 */
static int example_sensor_fetch_all(const struct device *dev)
{
	int ret;

	ret = example_sensor_fetch_temp(dev);
	if (ret < 0) {
		return ret;
	}

	return example_sensor_fetch_humidity(dev);
}

/*
 * Generate sample_fetch function with channel dispatch:
 * - example_sensor_sample_fetch()
 *
 * This macro creates a sample_fetch function that routes to the appropriate
 * fetch function based on the requested channel. It automatically handles
 * the switch statement and error return for unsupported channels.
 */
SENSOR_SAMPLE_FETCH_SIMPLE(example_sensor,
	SENSOR_CHAN_ALL, example_sensor_fetch_all,
	SENSOR_CHAN_AMBIENT_TEMP, example_sensor_fetch_temp,
	SENSOR_CHAN_HUMIDITY, example_sensor_fetch_humidity)

/**
 * Get channel reading
 *
 * Returns the cached sensor value for the requested channel.
 * This is manually implemented because we need to handle multiple channels
 * with different data fields.
 *
 * @param dev Pointer to the device structure
 * @param chan Sensor channel to read
 * @param val Pointer to store the sensor value
 * @return 0 on success, negative errno on failure
 */
static int example_sensor_channel_get(const struct device *dev,
				      enum sensor_channel chan,
				      struct sensor_value *val)
{
	struct example_sensor_data *data = dev->data;

	switch (chan) {
	case SENSOR_CHAN_AMBIENT_TEMP:
		*val = data->temp;
		break;
	case SENSOR_CHAN_HUMIDITY:
		*val = data->humidity;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

/* Define sensor driver API structure */
static DEVICE_API(sensor, example_sensor_driver_api) = {
	.sample_fetch = example_sensor_sample_fetch,
	.channel_get = example_sensor_channel_get,
};

/*
 * Generate a no-op power management handler:
 * - example_sensor_pm_action()
 *
 * This macro creates a PM action function that accepts all standard PM
 * actions but performs no operations. This is suitable for simple sensors
 * that don't require custom power management logic.
 */
SENSOR_PM_ACTION_NOOP(example_sensor)

/**
 * Initialize the sensor device
 *
 * Performs device initialization including:
 * - Checking I2C bus readiness
 * - Initializing power management
 * - Configuring the sensor
 *
 * @param dev Pointer to the device structure
 * @return 0 on success, negative errno on failure
 */
static int example_sensor_init(const struct device *dev)
{
	const struct example_sensor_config *cfg = dev->config;
	uint8_t config_val;
	int ret;

	/*
	 * Check if I2C bus is ready using helper macro.
	 * This expands to:
	 *   if (!device_is_ready(cfg->i2c.bus)) {
	 *       LOG_ERR("I2C bus not ready");
	 *       return -ENODEV;
	 *   }
	 */
	SENSOR_DEVICE_I2C_BUS_READY_CHECK(dev, cfg, i2c);

	/*
	 * Initialize power management using helper macro.
	 * This expands to PM initialization code when
	 * CONFIG_PM_DEVICE_RUNTIME is enabled.
	 */
	SENSOR_DEVICE_PM_INIT(dev);

	/* Configure the sensor for continuous operation */
	config_val = EXAMPLE_CONFIG_ENABLE | EXAMPLE_CONFIG_CONTINUOUS;
	ret = example_sensor_reg_write(dev, EXAMPLE_REG_CONFIG, &config_val, 1);
	if (ret < 0) {
		LOG_ERR("Failed to configure sensor");
		return ret;
	}

	LOG_INF("Example sensor initialized");
	return 0;
}

/*
 * Device instantiation macro for I2C sensors.
 *
 * This macro generates all the boilerplate code for device registration:
 * - Static data structure instance
 * - Static config structure instance with I2C spec
 * - Power management device definition
 * - Sensor device definition
 *
 * Without this macro, you would need approximately 7-10 lines of
 * repetitive code per device instance.
 */
#define EXAMPLE_SENSOR_DEFINE(inst)					\
	SENSOR_DEVICE_DT_INST_I2C_DEFINE(example_sensor, inst,		\
					 example_sensor_data,		\
					 example_sensor_config,		\
					 example_sensor_init,		\
					 example_sensor_pm_action,	\
					 example_sensor_driver_api)

/* Instantiate a device for each enabled device tree node */
DT_INST_FOREACH_STATUS_OKAY(EXAMPLE_SENSOR_DEFINE)
