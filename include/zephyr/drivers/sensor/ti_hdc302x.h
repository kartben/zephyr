/*
 * Copyright (c) 2025 Psicontrol N.V.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for HDC302X Temperature Sensors
 * @ingroup sensor_interface_ext_ti
 *
 * This exposes attributes for the HDC302X which can be used for
 * setting the Low power parameters.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_TI_HDC302X_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_TI_HDC302X_H_

#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Extended sensor attributes for the HDC302X */
enum sensor_attribute_hdc302x {
	/**
	 * @brief Sensor low power mode
	 *
	 * Can only be set to one of the modes of @ref sensor_power_mode_hdc302x.
	 * See datasheet for more info on the different modes.
	 */
	SENSOR_ATTR_POWER_MODE = SENSOR_ATTR_PRIV_START + 1,

	/**
	 * @brief Sensor automatic measurement mode
	 *
	 * Can only be set to one of the intervals of
	 * @ref sensor_measurement_interval_hdc302x.
	 */
	SENSOR_ATTR_INTEGRATION_TIME,
	/** Raw content of the sensor status register (get only) */
	SENSOR_ATTR_STATUS_REGISTER,
	/** Heater level, from 0 (off) to 14 (maximum) */
	SENSOR_ATTR_HEATER_LEVEL,
};

/** Low power modes for the HDC302X, selected with @ref SENSOR_ATTR_POWER_MODE */
enum sensor_power_mode_hdc302x {
	HDC302X_SENSOR_POWER_MODE_0, /**< Power mode 0 (lowest noise) */
	HDC302X_SENSOR_POWER_MODE_1, /**< Power mode 1 */
	HDC302X_SENSOR_POWER_MODE_2, /**< Power mode 2 */
	HDC302X_SENSOR_POWER_MODE_3, /**< Power mode 3 (lowest power) */

	HDC302X_SENSOR_POWER_MODE_MAX /**< Number of power modes */
};

/**
 * Automatic measurement intervals for the HDC302X, selected with the
 * HDC302X SENSOR_ATTR_INTEGRATION_TIME attribute
 */
enum sensor_measurement_interval_hdc302x {
	HDC302X_SENSOR_MEAS_INTERVAL_MANUAL, /**< Manual mode */
	HDC302X_SENSOR_MEAS_INTERVAL_0_5,    /**< 1 measurement per 2 seconds */
	HDC302X_SENSOR_MEAS_INTERVAL_1,      /**< 1 measurement per second */
	HDC302X_SENSOR_MEAS_INTERVAL_2,      /**< 2 measurements per second */
	HDC302X_SENSOR_MEAS_INTERVAL_4,      /**< 4 measurements per second */
	HDC302X_SENSOR_MEAS_INTERVAL_10,     /**< 10 measurements per second */

	HDC302X_SENSOR_MEAS_INTERVAL_MAX /**< Number of measurement intervals */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_TI_HDC302X_H_ */
