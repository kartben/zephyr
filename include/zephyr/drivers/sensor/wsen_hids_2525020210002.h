/*
 * Copyright (c) 2024 Würth Elektronik eiSos GmbH & Co. KG
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for WSEN-HIDS-2525020210002 Sensor
 * @ingroup wsen_hids_2525020210002_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_WSEN_HIDS_2525020210002_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_WSEN_HIDS_2525020210002_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/sensor.h>

/**
 * @brief WSEN-HIDS-2525020210002 sensor extensions.
 * @defgroup wsen_hids_2525020210002_interface WSEN-HIDS-2525020210002
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief WSEN-HIDS-specific sensor attributes.
 */
enum sensor_attribute_wsen_hids_2525020210002 {
	/** Set the conversion precision mode. */
	SENSOR_ATTR_WSEN_HIDS_2525020210002_PRECISION = SENSOR_ATTR_PRIV_START,
	/** Set the integrated heater mode. */
	SENSOR_ATTR_WSEN_HIDS_2525020210002_HEATER
};

/**
 * @brief WSEN-HIDS precision options.
 */
typedef enum {
	/** Select low precision mode. */
	hids_2525020210002_precision_Low = 0x0,
	/** Select medium precision mode. */
	hids_2525020210002_precision_Medium = 0x1,
	/** Select high precision mode. */
	hids_2525020210002_precision_High = 0x2
} hids_2525020210002_precision_t;

/**
 * @brief WSEN-HIDS heater operating modes.
 */
typedef enum {
	/** Disable heater output. */
	hids_2525020210002_heater_Off = 0x0,
	/** Enable 200 mW heater pulse for 1 s. */
	hids_2525020210002_heater_On_200mW_1s = 0x1,
	/** Enable 200 mW heater pulse for 100 ms. */
	hids_2525020210002_heater_On_200mW_100ms = 0x2,
	/** Enable 110 mW heater pulse for 1 s. */
	hids_2525020210002_heater_On_110mW_1s = 0x3,
	/** Enable 110 mW heater pulse for 100 ms. */
	hids_2525020210002_heater_On_110mW_100ms = 0x4,
	/** Enable 20 mW heater pulse for 1 s. */
	hids_2525020210002_heater_On_20mW_1s = 0x5,
	/** Enable 20 mW heater pulse for 100 ms. */
	hids_2525020210002_heater_On_20mW_100ms = 0x6,
} hids_2525020210002_heater_t;

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_WSEN_HIDS_2525020210002_H_ */
