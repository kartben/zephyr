/*
 * Copyright (c) 2025 Psicontrol N.V.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for HDC302X Temperature Sensors
 * @ingroup ti_hdc302x_interface
 *
 * This exposes attributes for the HDC302X which can be used for
 * setting the Low power parameters.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_HDC302X_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_HDC302X_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief TI HDC302X sensor extensions.
 * @defgroup ti_hdc302x_interface TI HDC302X
 * @ingroup sensor_interface_ext
 * @{
 */

/** Status register alert summary bit. */
#define TI_HDC302X_STATUS_REG_BIT_ALERT         0x8000
/** Status register heater-on indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_HEATER_ON     0x2000
/** Status register humidity alert indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_RH_ALERT      0x0800
/** Status register temperature alert indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_TEMP_ALERT    0x0400
/** Status register high-humidity threshold indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_RH_HIGH_ALERT 0x0200
/** Status register low-humidity threshold indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_RH_LOW_ALERT  0x0100

/** Status register high-temperature threshold indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_TEMP_HIGH_ALERT 0x0080
/** Status register low-temperature threshold indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_TEMP_LOW_ALERT  0x0040
/** Status register reset-detected indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_RESET_DETECTED  0x0010
/** Status register CRC-failed indicator bit. */
#define TI_HDC302X_STATUS_REG_BIT_CRC_FAILED      0x0001

/**
 * @brief HDC302X-specific sensor attributes.
 */
enum sensor_attribute_hdc302x {
	/** Set low-power mode.
	 *
	 * Use one of:
	 * - HDC302X_SENSOR_POWER_MODE_0
	 * - HDC302X_SENSOR_POWER_MODE_1
	 * - HDC302X_SENSOR_POWER_MODE_2
	 * - HDC302X_SENSOR_POWER_MODE_3
	 */
	SENSOR_ATTR_POWER_MODE = SENSOR_ATTR_PRIV_START + 1,

	/** Set automatic measurement interval.
	 *
	 * Use one of:
	 * - HDC302X_SENSOR_MEAS_INTERVAL_MANUAL
	 * - HDC302X_SENSOR_MEAS_INTERVAL_0_5
	 * - HDC302X_SENSOR_MEAS_INTERVAL_1
	 * - HDC302X_SENSOR_MEAS_INTERVAL_2
	 * - HDC302X_SENSOR_MEAS_INTERVAL_4
	 * - HDC302X_SENSOR_MEAS_INTERVAL_10
	 */
	SENSOR_ATTR_INTEGRATION_TIME,
	/** Read the sensor status register value. */
	SENSOR_ATTR_STATUS_REGISTER,
	/** Set heater level from 0 to 14. */
	SENSOR_ATTR_HEATER_LEVEL,
};

/**
 * @brief HDC302X low-power operating modes.
 */
enum sensor_power_mode_hdc302x {
	/** Select mode 0 (lowest noise). */
	HDC302X_SENSOR_POWER_MODE_0,
	/** Select mode 1. */
	HDC302X_SENSOR_POWER_MODE_1,
	/** Select mode 2. */
	HDC302X_SENSOR_POWER_MODE_2,
	/** Select mode 3 (lowest power). */
	HDC302X_SENSOR_POWER_MODE_3,

	/** Number of defined power modes. */
	HDC302X_SENSOR_POWER_MODE_MAX
};

/**
 * @brief HDC302X automatic measurement intervals.
 */
enum sensor_measurement_interval_hdc302x {
	/** Use manual measurement mode. */
	HDC302X_SENSOR_MEAS_INTERVAL_MANUAL,
	/** Measure once every 2 seconds (0.5 Hz). */
	HDC302X_SENSOR_MEAS_INTERVAL_0_5,
	/** Measure once per second (1 Hz). */
	HDC302X_SENSOR_MEAS_INTERVAL_1,
	/** Measure twice per second (2 Hz). */
	HDC302X_SENSOR_MEAS_INTERVAL_2,
	/** Measure four times per second (4 Hz). */
	HDC302X_SENSOR_MEAS_INTERVAL_4,
	/** Measure ten times per second (10 Hz). */
	HDC302X_SENSOR_MEAS_INTERVAL_10,

	/** Number of defined measurement intervals. */
	HDC302X_SENSOR_MEAS_INTERVAL_MAX
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_HDC302X_H_ */
