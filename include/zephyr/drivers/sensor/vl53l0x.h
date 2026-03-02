/*
 * Copyright (c) 2024 Michal Piekos
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Custom channels and values for VL53L0X ToF Sensor
 * @ingroup vl53l0x_interface
 *
 * These channels provide additional sensor data not covered by the standard
 * Zephyr sensor channels. Application must include vl53l0x.h file to gain
 * access to these channels.
 *
 * Example usage:
 * @code{c}
 * #include <zephyr/drivers/sensor/vl53l0x.h>
 *
 * if (sensor_channel_get(dev, SENSOR_CHAN_VL53L0X_RANGE_STATUS, &value)) {
 *	printk("Status: %d\n", value.val1);
 * }
 * @endcode
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_VL53L0X_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_VL53L0X_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief VL53L0X sensor extensions.
 * @defgroup vl53l0x_interface VL53L0X
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief VL53L0X-specific sensor channels.
 */
enum sensor_channel_vl53l0x {
	/** Report the estimated maximum measurable distance. */
	SENSOR_CHAN_VL53L0X_RANGE_DMAX = SENSOR_CHAN_PRIV_START,
	/** Report return signal rate in counts per second. */
	SENSOR_CHAN_VL53L0X_SIGNAL_RATE_RTN_CPS,
	/** Report ambient return rate in counts per second. */
	SENSOR_CHAN_VL53L0X_AMBIENT_RATE_RTN_CPS,
	/** Report the effective SPAD return count. */
	SENSOR_CHAN_VL53L0X_EFFECTIVE_SPAD_RTN_COUNT,
	/** Report the ranging status code. */
	SENSOR_CHAN_VL53L0X_RANGE_STATUS,
};

/**
 * @name VL53L0X range status values
 * @{
 */
/** Range measurement is valid. */
#define VL53L0X_RANGE_STATUS_RANGE_VALID    (0)
/** Sigma check failed. */
#define VL53L0X_RANGE_STATUS_SIGMA_FAIL	    (1)
/** Signal check failed. */
#define VL53L0X_RANGE_STATUS_SIGNAL_FAIL    (2)
/** Minimum range check failed. */
#define VL53L0X_RANGE_STATUS_MIN_RANGE_FAIL (3)
/** Phase check failed. */
#define VL53L0X_RANGE_STATUS_PHASE_FAIL	    (4)
/** Hardware failure occurred. */
#define VL53L0X_RANGE_STATUS_HARDWARE_FAIL  (5)
/** No updated result is available. */
#define VL53L0X_RANGE_STATUS_NO_UPDATE      (255)
/** @} */

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_VL53L0X_H_ */
