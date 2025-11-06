/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "charge_sim.h"
#include <zephyr/random/random.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charge_sim, LOG_LEVEL_DBG);

/* Charging constants */
#define CHARGING_VOLTAGE_V  480
#define CHARGING_CURRENT_A  120
#define CHARGING_POWER_W    (CHARGING_VOLTAGE_V * CHARGING_CURRENT_A)
/* Base meter reading per connector (Wh) */
#define BASE_METER_WH       10000
/* Battery capacity (assumed 50 kWh for typical EV) */
#define BATTERY_CAPACITY_WH 10000
/* Starting state of charge percentage */
#define START_SOC_PERCENT   20

/* Jitter ranges */
#define VOLTAGE_JITTER_V 10  /* ±10V */
#define CURRENT_JITTER_A 1   /* ±1A */
#define POWER_JITTER_W   200 /* ±200W */

void charge_sim_init_state(struct charge_sim_state *state, int conn_idx)
{
	if (state == NULL) {
		return;
	}

	state->start_time_ms = 0;
	state->start_meter_wh = 0;
	state->start_soc_percent = START_SOC_PERCENT;
	state->is_charging = false;
}

void charge_sim_start_session(struct charge_sim_state *state, int conn_idx, int64_t current_time_ms)
{
	if (state == NULL) {
		return;
	}

	state->start_time_ms = current_time_ms;
	state->start_meter_wh = BASE_METER_WH + (conn_idx * 1000);
	state->start_soc_percent = START_SOC_PERCENT;
	state->is_charging = true;

	LOG_DBG("Started charging session: conn=%d, start_meter=%u Wh, start_soc=%u%%", conn_idx,
		state->start_meter_wh, state->start_soc_percent);
}

void charge_sim_stop_session(struct charge_sim_state *state)
{
	if (state == NULL) {
		return;
	}

	state->is_charging = false;
	LOG_DBG("Stopped charging session");
}

uint32_t charge_sim_get_meter_value(const struct charge_sim_state *state, int conn_idx,
				    int64_t current_time_ms)
{
	int64_t elapsed_ms;
	double elapsed_hours;
	uint32_t energy_wh;
	uint32_t meter_value;

	if (state == NULL) {
		return BASE_METER_WH + (conn_idx * 1000);
	}

	if (!state->is_charging || state->start_time_ms == 0) {
		/* Not charging, return base meter reading */
		return BASE_METER_WH + (conn_idx * 1000);
	}

	/* Calculate elapsed time since start */
	elapsed_ms = current_time_ms - state->start_time_ms;
	elapsed_hours = (double)elapsed_ms / 3600000.0; /* Convert ms to hours */

	/* Calculate energy consumed: Power (W) * Time (h) = Energy (Wh) */
	energy_wh = (uint32_t)(CHARGING_POWER_W * elapsed_hours);

	/* Add jitter: ±1% of energy consumed */
	/* Generate jitter: -1% to +1% (99% to 101% of energy) */
	int jitter_percent = (sys_rand32_get() % 3) - 1; /* -1, 0, or +1 */
	/* Calculate jitter as signed to handle negative values correctly */
	int32_t jitter_wh_signed = (int32_t)(((int64_t)energy_wh * jitter_percent) / 100);

	/* Current meter reading = start meter + energy consumed (with jitter) */
	/* Use signed arithmetic to handle negative jitter, then clamp to ensure
	 * meter value never goes below start meter value
	 */
	int64_t meter_value_signed =
		(int64_t)state->start_meter_wh + (int64_t)energy_wh + jitter_wh_signed;
	if (meter_value_signed < (int64_t)state->start_meter_wh) {
		meter_value_signed = (int64_t)state->start_meter_wh;
	}
	meter_value = (uint32_t)meter_value_signed;

	return meter_value;
}

uint8_t charge_sim_get_soc_percent(const struct charge_sim_state *state, int64_t current_time_ms)
{
	int64_t elapsed_ms;
	double elapsed_hours;
	uint32_t energy_wh;
	uint32_t soc_percent;

	if (state == NULL) {
		return START_SOC_PERCENT;
	}

	if (!state->is_charging || state->start_time_ms == 0) {
		/* Not charging, return starting SOC */
		return state->start_soc_percent;
	}

	/* Calculate elapsed time since start */
	elapsed_ms = current_time_ms - state->start_time_ms;
	elapsed_hours = (double)elapsed_ms / 3600000.0; /* Convert ms to hours */

	/* Calculate energy consumed: Power (W) * Time (h) = Energy (Wh) */
	energy_wh = (uint32_t)(CHARGING_POWER_W * elapsed_hours);

	/* Calculate SOC increase: (energy_wh / battery_capacity_wh) * 100 */
	/* SOC = start_soc + (energy_consumed / battery_capacity * 100) */
	soc_percent =
		state->start_soc_percent + (uint32_t)((energy_wh * 100UL) / BATTERY_CAPACITY_WH);

	/* Cap SOC at 100% */
	if (soc_percent > 100) {
		soc_percent = 100;
	}

	return soc_percent;
}

int32_t charge_sim_get_voltage(bool is_charging)
{
	int32_t voltage_v;

	if (!is_charging) {
		return 0;
	}

	/* Return voltage value with jitter (in V) */
	voltage_v = CHARGING_VOLTAGE_V;
	int32_t jitter = (sys_rand32_get() % (2 * VOLTAGE_JITTER_V + 1)) - VOLTAGE_JITTER_V;
	voltage_v = voltage_v + jitter;

	/* Ensure voltage doesn't go negative */
	if (voltage_v < 0) {
		voltage_v = 0;
	}

	return voltage_v;
}

int32_t charge_sim_get_current(bool is_charging)
{
	int32_t current_a;

	if (!is_charging) {
		return 0;
	}

	/* Return current value with jitter (in A) */
	current_a = CHARGING_CURRENT_A;
	int32_t jitter = (sys_rand32_get() % (2 * CURRENT_JITTER_A + 1)) - CURRENT_JITTER_A;
	current_a = current_a + jitter;

	/* Ensure current doesn't go negative */
	if (current_a < 0) {
		current_a = 0;
	}

	return current_a;
}

int32_t charge_sim_get_power(bool is_charging)
{
	int32_t power_w;

	if (!is_charging) {
		return 0;
	}

	/* Return power value with jitter */
	power_w = CHARGING_POWER_W;
	int32_t jitter = (sys_rand32_get() % (2 * POWER_JITTER_W + 1)) - POWER_JITTER_W;
	power_w = power_w + jitter;

	/* Ensure power doesn't go negative */
	if (power_w < 0) {
		power_w = 0;
	}

	return power_w;
}

void charge_sim_get_all_params(const struct charge_sim_state *state, int conn_idx,
			       int64_t current_time_ms, struct charge_sim_params *params)
{
	if (params == NULL) {
		return;
	}

	params->meter_wh = charge_sim_get_meter_value(state, conn_idx, current_time_ms);
	params->soc_percent = charge_sim_get_soc_percent(state, current_time_ms);
	params->voltage_v = charge_sim_get_voltage(state ? state->is_charging : false);
	params->current_a = charge_sim_get_current(state ? state->is_charging : false);
	params->power_w = charge_sim_get_power(state ? state->is_charging : false);
}

uint32_t charge_sim_get_start_meter(const struct charge_sim_state *state)
{
	if (state == NULL) {
		return 0;
	}

	return state->start_meter_wh;
}
