/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor API of the Seeed MR60BHA2 sensor
 * @ingroup mr60bha2_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_

/**
 * @brief Seeed MR60BHA2 60 GHz mmWave radar
 * @defgroup mr60bha2_interface Seeed MR60BHA2
 * @ingroup sensor_interface_ext
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/sensor.h>

/**
 * @brief Custom sensor channels for MR60BHA2.
 */
enum sensor_channel_mr60bha2 {
	/** Breathing rate in breaths per minute. */
	SENSOR_CHAN_MR60BHA2_BREATH_RATE = SENSOR_CHAN_PRIV_START,
	/** Heart rate in beats per minute. */
	SENSOR_CHAN_MR60BHA2_HEART_RATE,
	/** Number of tracked targets. */
	SENSOR_CHAN_MR60BHA2_TARGET_COUNT,
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_ */
