/*
 * Copyright (c) 2023 Kurtis Dinelle
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for AMS's TSL2591 ambient light sensor
 * @ingroup sensor_interface_ext_ams
 *
 * This exposes attributes for the TSL2591 which can be used for
 * setting the on-chip gain, integration time, and persist filter parameters.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2591_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2591_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Extended sensor attributes for the TSL2591 */
enum sensor_attribute_tsl2591 {
	/**
	 * @brief Sensor ADC gain mode
	 *
	 * Rather than set this value directly, can only be set to operate in
	 * one of the four modes of @ref sensor_gain_tsl2591.
	 *
	 * See datasheet for actual typical gain scales these modes correspond to.
	 */
	SENSOR_ATTR_GAIN_MODE = SENSOR_ATTR_PRIV_START + 1,

	/**
	 * @brief Sensor ADC integration time, in milliseconds
	 *
	 * Can only be set to one of six values:
	 *
	 * 100, 200, 300, 400, 500, or 600
	 */
	SENSOR_ATTR_INTEGRATION_TIME,

	/**
	 * @brief Sensor ALS interrupt persist filter
	 *
	 * Represents the number of consecutive sensor readings outside of a set
	 * threshold before triggering an interrupt. Can only be set to one of
	 * sixteen values:
	 *
	 * 0, 1, 2, 3, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, or 60
	 *
	 * Setting this to 0 causes an interrupt to generate every ALS cycle,
	 * regardless of threshold.
	 * Setting this to 1 is equivalent to the no-persist interrupt mode.
	 */
	SENSOR_ATTR_INT_PERSIST
};

/** Gain modes for the TSL2591, set with @ref SENSOR_ATTR_GAIN_MODE */
enum sensor_gain_tsl2591 {
	TSL2591_SENSOR_GAIN_LOW,  /**< Low gain mode */
	TSL2591_SENSOR_GAIN_MED,  /**< Medium gain mode */
	TSL2591_SENSOR_GAIN_HIGH, /**< High gain mode */
	TSL2591_SENSOR_GAIN_MAX   /**< Maximum gain mode */
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2591_H_ */
