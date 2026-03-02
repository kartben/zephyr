/*
 * Copyright (c) 2022 ITE Technology Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for IT8XXX2 voltage comparator.
 * @ingroup it8xxx2_vcmp_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_IT8XXX2_VCMP_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_IT8XXX2_VCMP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/sensor.h>

/**
 * @brief IT8XXX2 voltage comparator extensions.
 * @defgroup it8xxx2_vcmp_interface IT8XXX2 VCMP
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief IT8XXX2 voltage comparator-specific sensor attributes.
 */
enum it8xxx2_vcmp_sensor_attribute {
	/** Set the lower threshold voltage. */
	SENSOR_ATTR_LOWER_VOLTAGE_THRESH = SENSOR_ATTR_PRIV_START,
	/** Set the upper threshold voltage. */
	SENSOR_ATTR_UPPER_VOLTAGE_THRESH,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_IT8XXX2_VCMP_H_ */
