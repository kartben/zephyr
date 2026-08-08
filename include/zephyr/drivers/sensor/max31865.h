/*
 * Copyright (c) 2023 SILA Embedded Solutions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor attributes of the MAX31865 RTD temperature sensor.
 * @ingroup sensor_interface_ext_adi
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31865_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31865_H_

/**
 * @brief Enable or disable the three-wire RTD connection mode
 *
 * Use sensor_value.val1 to enable (1) or disable (0) the three-wire
 * connection mode. When disabled, a two-wire or four-wire connection
 * is used.
 */
#define SENSOR_ATTR_MAX31865_THREE_WIRE SENSOR_ATTR_PRIV_START

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX31865_H_ */
