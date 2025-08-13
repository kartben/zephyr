/*
 * Copyright (c) 2022 T-Mobile USA, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for AMS's TSL2540 ambient light sensor
 *
 * This exposes attributes for the TSL2540 which can be used for
 * setting the on-chip gain and integration time parameters.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2540_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2540_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Custom attributes for the TSL2540 sensor.
 */
enum sensor_attribute_tsl2540 {
	/**
	 * @brief Sensor integration time
	 *
	 * Sets the integration time, in milliseconds.
	 */
	SENSOR_ATTR_INTEGRATION_TIME = SENSOR_ATTR_PRIV_START + 1,
	/**
	 * @brief Sensor ALS interrupt persistence filter
	 *
	 * Enable the interrupt persistence filter (value is 0-15, representing the number of
	 * samples to average).
	 */
	SENSOR_ATTR_INT_APERS,
	/**
	 * @brief Continuous conversion mode
	 *
	 * Enable continuous conversion mode (no attribute value is required).
	 */
	SENSOR_ATTR_TSL2540_CONTINUOUS_MODE,
	/**
	 * @brief Continuous conversion mode without wait
	 *
	 * Enable continuous conversion mode without wait (no attribute value is required).
	 */
	SENSOR_ATTR_TSL2540_CONTINUOUS_NO_WAIT_MODE,
};

/**
 * @name Possible values for SENSOR_ATTR_GAIN
 * @{
 */
enum sensor_gain_tsl2540 {
	TSL2540_SENSOR_GAIN_1_2,	/**< 1/2x gain */
	TSL2540_SENSOR_GAIN_1,		/**< 1x gain */
	TSL2540_SENSOR_GAIN_4,		/**< 4x gain */
	TSL2540_SENSOR_GAIN_16,		/**< 16x gain */
	TSL2540_SENSOR_GAIN_64,		/**< 64x gain */
	TSL2540_SENSOR_GAIN_128,	/**< 128x gain */
};
/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2540_H_ */
