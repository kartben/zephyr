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
 * @ingroup icm4268x_interface
 *
 * Pin function configuration via attributes under the current sensor driver abstraction.
 */

/**
 * @brief ICM4268X sensor extensions.
 * @defgroup icm4268x_interface ICM4268X
 * @ingroup sensor_interface_ext
 * @{
 */

/** Configure PIN9 as INT2 output. */
#define ICM4268X_PIN9_FUNCTION_INT2  0
/** Configure PIN9 as FSYNC input. */
#define ICM4268X_PIN9_FUNCTION_FSYNC 1
/** Configure PIN9 as CLKIN input. */
#define ICM4268X_PIN9_FUNCTION_CLKIN 2

/**
 * @brief Extended sensor attributes for ICM4268X
 *
 * Attributes for setting pin function.
 */
enum sensor_attribute_icm4268x {
	/** Select the PIN9 function. */
	SENSOR_ATTR_ICM4268X_PIN9_FUNCTION = SENSOR_ATTR_PRIV_START
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_ICM4268X_H_ */
