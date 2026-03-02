/*
 * Copyright (c) 2023 SILA Embedded Solutions
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for MAX31865 sensor attributes.
 * @ingroup max31865_interface
 */

#ifndef _MAX31865_PUB_H
#define _MAX31865_PUB_H

/**
 * @brief MAX31865 sensor extensions.
 * @defgroup max31865_interface MAX31865
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief Enable three-wire RTD mode.
 */
#define SENSOR_ATTR_MAX31865_THREE_WIRE SENSOR_ATTR_PRIV_START

/**
 * @}
 */

#endif /* _MAX31865_PUB_H */
