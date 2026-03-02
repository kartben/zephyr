/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for nPM2100 VBAT measurements.
 * @ingroup npm2100_vbat_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM2100_VBAT_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM2100_VBAT_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief nPM2100 VBAT extensions.
 * @defgroup npm2100_vbat_interface nPM2100 VBAT
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief nPM2100 VBAT-specific sensor channels.
 */
enum sensor_channel_npm2100_vbat {
	/** Report VBAT status flags. */
	SENSOR_CHAN_NPM2100_VBAT_STATUS = SENSOR_CHAN_PRIV_START,
	/** Report voltage droop events. */
	SENSOR_CHAN_NPM2100_VOLT_DROOP,
	/** Report dynamic power scaling (DPS) event count. */
	SENSOR_CHAN_NPM2100_DPS_COUNT,
	/** Report DPS timer value. */
	SENSOR_CHAN_NPM2100_DPS_TIMER,
	/** Report DPS active duration. */
	SENSOR_CHAN_NPM2100_DPS_DURATION,
};

/**
 * @brief nPM2100 VBAT-specific sensor attributes.
 */
enum sensor_attr_npm2100_vbat {
	/** Configure the ADC start delay. */
	SENSOR_ATTR_NPM2100_ADC_DELAY = SENSOR_ATTR_PRIV_START,
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM2100_VBAT_H_ */
