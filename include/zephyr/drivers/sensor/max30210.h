/*
 * Copyright (c) 2025 Analog Devices Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for MAX30210 temperature sensor controls.
 * @ingroup max30210_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX30210_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX30210_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief MAX30210 temperature sensor extensions.
 * @defgroup max30210_interface MAX30210
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief MAX30210-specific sensor attributes.
 */
enum sensor_attribute_max30210 {
	/** Set the interrupt polarity. */
	SENSOR_ATTR_MAX30210_INTERRUPT_POLARITY = SENSOR_ATTR_PRIV_START,
	/** Enable or disable continuous conversion mode. */
	SENSOR_ATTR_MAX30210_CONTINUOUS_CONVERSION_MODE,
	/** Set the fast temperature increase threshold. */
	SENSOR_ATTR_MAX30210_TEMP_INC_FAST_THRESH,
	/** Set the fast temperature decrease threshold. */
	SENSOR_ATTR_MAX30210_TEMP_DEC_FAST_THRESH,
	/** Configure the rate-of-change filter. */
	SENSOR_ATTR_MAX30210_RATE_CHG_FILTER,
	/** Set the high threshold trip count. */
	SENSOR_ATTR_MAX30210_HI_TRIP_COUNT,
	/** Set the low threshold trip count. */
	SENSOR_ATTR_MAX30210_LO_TRIP_COUNT,
	/** Reset the high threshold trip count. */
	SENSOR_ATTR_MAX30210_HI_TRIP_COUNT_RESET,
	/** Reset the low threshold trip count. */
	SENSOR_ATTR_MAX30210_LO_TRIP_COUNT_RESET,
	/** Enable or disable non-consecutive high threshold mode. */
	SENSOR_ATTR_MAX30210_HI_NON_CONSECUTIVE_MODE,
	/** Enable or disable non-consecutive low threshold mode. */
	SENSOR_ATTR_MAX30210_LO_NON_CONSECUTIVE_MODE,
	/** Set the upper temperature threshold. */
	SENSOR_ATTR_MAX30210_UPPER_THRESH,
	/** Configure the alert output mode. */
	SENSOR_ATTR_MAX30210_ALERT_MODE,
	/** Trigger a software reset. */
	SENSOR_ATTR_MAX30210_SOFTWARE_RESET,
	/** Start a temperature conversion. */
	SENSOR_ATTR_MAX30210_TEMP_CONVERT,
	/** Enable or disable auto mode operation. */
	SENSOR_ATTR_MAX30210_AUTO_MODE
};

/**
 * @brief MAX30210 sampling rates.
 */
enum sensor_sampling_rate_max30210 {
	/** Sample at 0.015625 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_015625 = 0,
	/** Sample at 0.03125 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_03125,
	/** Sample at 0.0625 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_0625,
	/** Sample at 0.125 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_125,
	/** Sample at 0.25 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_25,
	/** Sample at 0.5 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_0_5,
	/** Sample at 1 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_1,
	/** Sample at 2 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_2,
	/** Sample at 4 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_4,
	/** Sample at 8 Hz. */
	SENSOR_SAMPLING_RATE_MAX30210_8,
};

/**
 * @brief MAX30210-specific trigger types.
 */
enum sensor_trigger_type_max30210 {
	/** Trigger when temperature increases faster than configured threshold. */
	SENSOR_TRIG_TEMP_INC_FAST = SENSOR_TRIG_PRIV_START,
	/** Trigger when temperature decreases faster than configured threshold. */
	SENSOR_TRIG_TEMP_DEC_FAST,
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MAX30210_H_ */
