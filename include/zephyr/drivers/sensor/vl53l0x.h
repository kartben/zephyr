/*
 * Copyright (c) 2024 Michal Piekos
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Custom channels and values for VL53L0X ToF Sensor
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

/* VL53L0x specific channels */
enum sensor_channel_vl53l0x {
	/**
	 * @brief Maximum range
	 *
	 * Maximum range of the sensor, in millimeters.
	 */
	SENSOR_CHAN_VL53L0X_RANGE_DMAX = SENSOR_CHAN_PRIV_START,

	/**
	 * @brief Signal rate
	 *
	 * Signal rate, in counts per second.
	 */
	SENSOR_CHAN_VL53L0X_SIGNAL_RATE_RTN_CPS,

	/**
	 * @brief Ambient rate
	 *
	 * Ambient rate, in counts per second.
	 */
	SENSOR_CHAN_VL53L0X_AMBIENT_RATE_RTN_CPS,

	/**
	 * @brief Effective SPAD count
	 *
	 * Effective SPAD (Single Photon Avalanche Diode) count.
	 */
	SENSOR_CHAN_VL53L0X_EFFECTIVE_SPAD_RTN_COUNT,

	/**
	 * @brief Range status
	 *
	 * Range status, available in the sensor_value.val1 field.
	 * Possible values are defined as @a VL53L0X_RANGE_STATUS_* macros.
	 */
	SENSOR_CHAN_VL53L0X_RANGE_STATUS,
};

/**
 * @name Possible values for SENSOR_CHAN_VL53L0X_RANGE_STATUS channel
 *
 * @details See UM2039 User Manual, Rev. 2.0, Section 5.3.1 / Table 1.
 * @{
 */
#define VL53L0X_RANGE_STATUS_RANGE_VALID    (0)		/**< Range valid */
#define VL53L0X_RANGE_STATUS_SIGMA_FAIL	    (1)		/**< Sigma fail */
#define VL53L0X_RANGE_STATUS_SIGNAL_FAIL    (2)		/**< Signal fail */
#define VL53L0X_RANGE_STATUS_MIN_RANGE_FAIL (3)		/**< Min range fail */
#define VL53L0X_RANGE_STATUS_PHASE_FAIL	    (4)		/**< Phase fail */
#define VL53L0X_RANGE_STATUS_HARDWARE_FAIL  (5)		/**< Hardware fail */
#define VL53L0X_RANGE_STATUS_NO_UPDATE      (255)	/**< No update */

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_VL53L0X_H_ */
