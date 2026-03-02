/*
 * Copyright (c) 2022 T-Mobile USA, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Extended public API for AMS's TSL2540 ambient light sensor
 * @ingroup tsl2540_interface
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
 * @brief TSL2540 sensor extensions.
 * @defgroup tsl2540_interface TSL2540
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief TSL2540-specific sensor attributes.
 */
enum sensor_attribute_tsl2540 {
	/** Set sensor integration time in milliseconds. */
	SENSOR_ATTR_INTEGRATION_TIME = SENSOR_ATTR_PRIV_START + 1,
	/** Set ALS interrupt persistence filter. */
	SENSOR_ATTR_INT_APERS,
	/** Put the sensor into shutdown mode. */
	SENSOR_ATTR_TSL2540_SHUTDOWN_MODE,
	/** Enable continuous conversion mode. */
	SENSOR_ATTR_TSL2540_CONTINUOUS_MODE,
	/** Enable continuous conversion without wait intervals. */
	SENSOR_ATTR_TSL2540_CONTINUOUS_NO_WAIT_MODE,
};

/**
 * @brief TSL2540 gain modes.
 */
enum sensor_gain_tsl2540 {
	/** Set gain to 0.5x. */
	TSL2540_SENSOR_GAIN_1_2,
	/** Set gain to 1x. */
	TSL2540_SENSOR_GAIN_1,
	/** Set gain to 4x. */
	TSL2540_SENSOR_GAIN_4,
	/** Set gain to 16x. */
	TSL2540_SENSOR_GAIN_16,
	/** Set gain to 64x. */
	TSL2540_SENSOR_GAIN_64,
	/** Set gain to 128x. */
	TSL2540_SENSOR_GAIN_128,
};

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_TSL2540_H_ */
