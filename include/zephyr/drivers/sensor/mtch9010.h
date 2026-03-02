/*
 * Copyright (c) 2025 Microchip Technology Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for MTCH9010 touch/proximity channels.
 * @ingroup mtch9010_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MTCH9010_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MTCH9010_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>

/**
 * @brief MTCH9010 sensor extensions.
 * @defgroup mtch9010_interface MTCH9010
 * @ingroup sensor_interface_ext
 * @{
 */

/** Maximum valid raw MTCH9010 measurement result. */
#define MTCH9010_MAX_RESULT 65535
/** Minimum valid raw MTCH9010 measurement result. */
#define MTCH9010_MIN_RESULT 0

/**
 * @brief MTCH9010-specific sensor channels.
 */
enum sensor_channel_mtch9010 {
	/** Get the current state of the OUT signal line. */
	SENSOR_CHAN_MTCH9010_OUT_STATE = SENSOR_CHAN_PRIV_START,
	/** Get the computed OUT state derived from the previous result. */
	SENSOR_CHAN_MTCH9010_SW_OUT_STATE,
	/** Get the configured reference value. */
	SENSOR_CHAN_MTCH9010_REFERENCE_VALUE,
	/** Get the configured threshold value. */
	SENSOR_CHAN_MTCH9010_THRESHOLD_VALUE,
	/** Get the last measured result value. */
	SENSOR_CHAN_MTCH9010_MEAS_RESULT,
	/** Get the difference between the last result and reference. */
	SENSOR_CHAN_MTCH9010_MEAS_DELTA,
	/** Get whether the heartbeat indicates an error state. */
	SENSOR_CHAN_MTCH9010_HEARTBEAT_ERROR_STATE
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MTCH9010_H_ */
