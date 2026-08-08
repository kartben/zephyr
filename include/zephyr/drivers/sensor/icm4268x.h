/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_ICM4268X_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_ICM4268X_H_

#include <zephyr/drivers/sensor.h>

/**
 * @file
 * @brief Extended public API for ICM4268X
 * @ingroup sensor_interface_ext_tdk
 *
 * Pin function configuration via attributes under the current sensor driver abstraction.
 */

/**
 * @name Pin 9 functions
 * @{
 */

#define ICM4268X_PIN9_FUNCTION_INT2  0 /**< Pin 9 acts as interrupt output INT2 */
#define ICM4268X_PIN9_FUNCTION_FSYNC 1 /**< Pin 9 acts as FSYNC input */
#define ICM4268X_PIN9_FUNCTION_CLKIN 2 /**< Pin 9 acts as external clock input CLKIN */

/** @} */

/**
 * @brief Extended sensor attributes for ICM4268X
 *
 * Attributes for setting pin function.
 */
enum sensor_attribute_icm4268x {
	/**
	 * Pin 9 function selection.
	 *
	 * sensor_value.val1 is one of the ICM4268X_PIN9_FUNCTION_* values,
	 * sensor_value.val2 is the external clock frequency in Hz,
	 * from 31000 to 50000.
	 */
	SENSOR_ATTR_ICM4268X_PIN9_FUNCTION = SENSOR_ATTR_PRIV_START
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_ICM4268X_H_ */
