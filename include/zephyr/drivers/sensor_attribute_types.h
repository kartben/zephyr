/*
 * Copyright (c) 2023 Google LLC
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for sensor attribute value data types.
 * @ingroup sensor_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_ATTRIBUTE_TYPES_H
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_ATTRIBUTE_TYPES_H

#include <zephyr/dsp/types.h>
#include <zephyr/dsp/print_format.h>

#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Used by the following channel/attribute pairs:
 * - SENSOR_CHAN_ACCEL_XYZ
 *   - SENSOR_ATTR_OFFSET
 * - SENSOR_CHAN_GYRO_XYZ
 *   - SENSOR_ATTR_OFFSET
 * - SENSOR_CHAN_MAGN_XYZ
 *   - SENSOR_ATTR_OFFSET
 */
struct sensor_three_axis_attribute {
	/** Shift value for @c x, @c y and @c z */
	int8_t shift;
	/** Attribute value for each of the three axes */
	union {
		struct {
			q31_t x; /**< X-axis value */
			q31_t y; /**< Y-axis value */
			q31_t z; /**< Z-axis value */
		};
		q31_t values[3]; /**< Array of the X, Y and Z values */
	};
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_ATTRIBUTE_TYPES_H */
