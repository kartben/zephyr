/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SMART_SENSOR_HUB_SENSOR_SIM_H_
#define SMART_SENSOR_HUB_SENSOR_SIM_H_

/**
 * @file sensor_sim.h
 * @brief Simulated sensor module — reads synthetic environment data and
 *        publishes it on the zbus ``sensor_data_chan`` channel.
 */

/** Start the sensor simulation (called from main). */
void sensor_sim_start(void);

#endif /* SMART_SENSOR_HUB_SENSOR_SIM_H_ */
