/*
 * Copyright 2025 Nova Dynamics LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for INA2XX sensor channels and attributes.
 * @ingroup ina2xx_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_INA2XX_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_INA2XX_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief INA2XX sensor extensions.
 * @defgroup ina2xx_interface INA2XX
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief INA2XX extra sensor channels.
 */
enum sensor_channel_ina2xx {
	/** Accumulated energy, in Joules **/
	SENSOR_CHAN_INA2XX_ENERGY = SENSOR_CHAN_PRIV_START,

	/** Accumulated charge, in Coulombs **/
	SENSOR_CHAN_INA2XX_CHARGE,
};

/**
 * @brief INA2XX-specific sensor attributes.
 */
enum sensor_attribute_ina2xx {
	/** Configure the ADC operating mode. */
	SENSOR_ATTR_ADC_CONFIGURATION = SENSOR_ATTR_PRIV_START,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_INA2XX_H_ */
