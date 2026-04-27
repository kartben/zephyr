/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor API of Seeed Studio MR60BHA2 sensor
 * @ingroup mr60bha2_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_

/**
 * @brief Seeed Studio MR60BHA2 60 GHz mmWave radar
 * @defgroup mr60bha2_interface Seeed Studio MR60BHA2
 * @ingroup sensor_interface_ext
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/drivers/sensor.h>

/**
 * @brief Presence and movement status values reported by the MR60BHA2.
 */
enum mr60bha2_presence_status {
	MR60BHA2_PRESENCE_NO_TARGET = 0x00, /**< No target detected */
	MR60BHA2_PRESENCE_STATIC    = 0x01, /**< Stationary target detected */
	MR60BHA2_PRESENCE_MOVING    = 0x02, /**< Moving target detected */
};

/**
 * @brief Breathing detection phase values reported by the MR60BHA2.
 */
enum mr60bha2_breathing_phase {
	MR60BHA2_BREATHING_DETECTING = 0x00, /**< Detecting, no result yet */
	MR60BHA2_BREATHING_NORMAL    = 0x01, /**< Normal breathing */
	MR60BHA2_BREATHING_RAPID     = 0x02, /**< Rapid breathing */
	MR60BHA2_BREATHING_SLOW      = 0x03, /**< Slow breathing */
	MR60BHA2_BREATHING_NONE      = 0x04, /**< No breathing detected */
};

/**
 * @brief Heartbeat detection phase values reported by the MR60BHA2.
 */
enum mr60bha2_heartbeat_phase {
	MR60BHA2_HEARTBEAT_DETECTING = 0x00, /**< Detecting, no result yet */
	MR60BHA2_HEARTBEAT_NORMAL    = 0x01, /**< Normal heartbeat */
	MR60BHA2_HEARTBEAT_RAPID     = 0x02, /**< Rapid heartbeat */
	MR60BHA2_HEARTBEAT_SLOW      = 0x03, /**< Slow heartbeat */
	MR60BHA2_HEARTBEAT_NONE      = 0x04, /**< No heartbeat detected */
};

/**
 * @brief Custom sensor channels for MR60BHA2.
 */
enum sensor_channel_mr60bha2 {
	/** Human presence and movement status.
	 *
	 * @c sensor_value.val1 is an @ref mr60bha2_presence_status value.
	 * @c sensor_value.val2 is unused (always 0).
	 */
	SENSOR_CHAN_MR60BHA2_PRESENCE_STATUS = SENSOR_CHAN_PRIV_START,

	/** Breathing detection phase.
	 *
	 * @c sensor_value.val1 is an @ref mr60bha2_breathing_phase value.
	 * @c sensor_value.val2 is unused (always 0).
	 */
	SENSOR_CHAN_MR60BHA2_BREATHING_PHASE,

	/** Breathing rate in breaths per minute.
	 *
	 * @c sensor_value.val1 is the integer part (breaths/min).
	 * @c sensor_value.val2 is the fractional part in millionths of a breath/min.
	 * The protocol reports tenths of breaths/min, so the fractional resolution
	 * is 0.1 breaths/min.
	 */
	SENSOR_CHAN_MR60BHA2_BREATHING_RATE,

	/** Heartbeat detection phase.
	 *
	 * @c sensor_value.val1 is an @ref mr60bha2_heartbeat_phase value.
	 * @c sensor_value.val2 is unused (always 0).
	 */
	SENSOR_CHAN_MR60BHA2_HEARTBEAT_PHASE,

	/** Heartbeat rate in beats per minute.
	 *
	 * @c sensor_value.val1 is the integer bpm value.
	 * @c sensor_value.val2 is unused (always 0).
	 */
	SENSOR_CHAN_MR60BHA2_HEARTBEAT_RATE,
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MR60BHA2_H_ */
