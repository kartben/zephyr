/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SMART_SENSOR_HUB_ALERT_ENGINE_H_
#define SMART_SENSOR_HUB_ALERT_ENGINE_H_

/**
 * @file alert_engine.h
 * @brief Alert engine — subscribes to sensor data via zbus, evaluates
 *        thresholds and publishes alert events.  Uses scope-based cleanup
 *        guards for safe mutex handling.
 */

/** Start the alert engine (called from main). */
void alert_engine_start(void);

#endif /* SMART_SENSOR_HUB_ALERT_ENGINE_H_ */
