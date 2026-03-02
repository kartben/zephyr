/*
 * Copyright (c) 2024 SILA Embedded Solutions GmbH
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for MAX31790 fan controller measurements.
 * @ingroup max31790_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31790_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31790_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief MAX31790 fan controller extensions.
 * @defgroup max31790_interface MAX31790
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief MAX31790-specific sensor channels.
 */
enum sensor_channel_max31790 {
	/** Report fan fault status. */
	SENSOR_CHAN_MAX31790_FAN_FAULT = SENSOR_CHAN_PRIV_START,
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31790_H_ */
