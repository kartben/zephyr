/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file channels.h
 * @brief Zbus channel declarations for the Smart Sensor Hub.
 *
 * Demonstrates the Zephyr 4.4 zbus pub/sub API: channels are defined once and
 * shared across modules via ZBUS_CHAN_DECLARE.
 */

#ifndef SMART_SENSOR_HUB_CHANNELS_H_
#define SMART_SENSOR_HUB_CHANNELS_H_

#include <zephyr/zbus/zbus.h>

/** Sensor reading published by the sensor simulation module. */
struct sensor_reading {
	int32_t temperature_mcelsius; /**< Temperature in milli-°C */
	int32_t humidity_mpercent;    /**< Relative humidity in milli-% */
	int32_t pressure_mpa;        /**< Atmospheric pressure in milli-Pa */
	uint32_t seq;                /**< Monotonic sequence number */
};

/** Alert raised by the alert engine when a threshold is breached. */
struct alert_event {
	uint8_t severity;   /**< 0 = info, 1 = warning, 2 = critical */
	uint8_t sensor_id;  /**< Which sensor triggered the alert */
	int32_t value;      /**< The offending value */
	int32_t threshold;  /**< The threshold that was breached */
};

ZBUS_CHAN_DECLARE(sensor_data_chan, alert_chan);

#endif /* SMART_SENSOR_HUB_CHANNELS_H_ */
