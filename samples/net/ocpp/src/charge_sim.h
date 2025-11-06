/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef CHARGE_SIM_H
#define CHARGE_SIM_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Charging simulation module
 *
 * This module simulates EV charging with realistic energy consumption,
 * state of charge (SOC) progression, and electrical parameters with jitter.
 */

/* Per-connector charging session state */
struct charge_sim_state {
	int64_t start_time_ms;     /* Start time in milliseconds */
	uint32_t start_meter_wh;   /* Meter reading at start (Wh) */
	uint8_t start_soc_percent; /* State of charge at start (%) */
	bool is_charging;          /* Whether currently charging */
};

/* Charging electrical parameters */
struct charge_sim_params {
	uint32_t meter_wh;   /* Current meter reading (Wh) */
	uint8_t soc_percent; /* Current state of charge (%) */
	int32_t voltage_v;   /* Current voltage (V) */
	int32_t current_a;   /* Current current (A) */
	int32_t power_w;     /* Current power (W) */
};

/**
 * @brief Initialize charging simulation state for a connector
 *
 * @param state Pointer to connector state structure
 * @param conn_idx Connector index (0-based)
 */
void charge_sim_init_state(struct charge_sim_state *state, int conn_idx);

/**
 * @brief Start a charging session
 *
 * @param state Pointer to connector state structure
 * @param conn_idx Connector index (0-based)
 * @param current_time_ms Current time in milliseconds
 */
void charge_sim_start_session(struct charge_sim_state *state, int conn_idx,
			      int64_t current_time_ms);

/**
 * @brief Stop a charging session
 *
 * @param state Pointer to connector state structure
 */
void charge_sim_stop_session(struct charge_sim_state *state);

/**
 * @brief Calculate current meter value for a connector
 *
 * @param state Pointer to connector state structure
 * @param conn_idx Connector index (0-based)
 * @param current_time_ms Current time in milliseconds
 * @return Current meter reading in Wh
 */
uint32_t charge_sim_get_meter_value(const struct charge_sim_state *state, int conn_idx,
				    int64_t current_time_ms);

/**
 * @brief Calculate current state of charge percentage
 *
 * @param state Pointer to connector state structure
 * @param current_time_ms Current time in milliseconds
 * @return Current state of charge (0-100%)
 */
uint8_t charge_sim_get_soc_percent(const struct charge_sim_state *state, int64_t current_time_ms);

/**
 * @brief Calculate current voltage with jitter
 *
 * @param is_charging Whether the connector is currently charging
 * @return Current voltage in V (0 if not charging)
 */
int32_t charge_sim_get_voltage(bool is_charging);

/**
 * @brief Calculate current current with jitter
 *
 * @param is_charging Whether the connector is currently charging
 * @return Current current in A (0 if not charging)
 */
int32_t charge_sim_get_current(bool is_charging);

/**
 * @brief Calculate current power with jitter
 *
 * @param is_charging Whether the connector is currently charging
 * @return Current power in W (0 if not charging)
 */
int32_t charge_sim_get_power(bool is_charging);

/**
 * @brief Get all charging parameters at once
 *
 * @param state Pointer to connector state structure
 * @param conn_idx Connector index (0-based)
 * @param current_time_ms Current time in milliseconds
 * @param params Output structure to fill with current parameters
 */
void charge_sim_get_all_params(const struct charge_sim_state *state, int conn_idx,
			       int64_t current_time_ms, struct charge_sim_params *params);

/**
 * @brief Get the start meter value for a connector session
 *
 * @param state Pointer to connector state structure
 * @return Start meter reading in Wh
 */
uint32_t charge_sim_get_start_meter(const struct charge_sim_state *state);

#endif /* CHARGE_SIM_H */
