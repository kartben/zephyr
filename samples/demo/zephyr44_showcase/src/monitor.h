/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — environmental monitor module.
 */

#ifndef SECUREVAULT_MONITOR_H
#define SECUREVAULT_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Latest sensor reading snapshot. */
struct monitor_reading {
	/** Temperature in milli-°C (e.g. 22500 = 22.5 °C). */
	int32_t temp_mc;
	/** Relative humidity in milli-% (e.g. 48000 = 48.0 %). */
	int32_t hum_mpct;
	/** Sequence number, incremented on every successful poll. */
	uint32_t seq;
};

/**
 * @brief Initialise the monitor subsystem and start background threads.
 *
 * Launches three priority-differentiated worker threads whose combined
 * scheduler pressure drives the CPU frequency policy introduced in 4.4.
 *
 * @return 0 on success, negative errno on failure.
 */
int monitor_init(void);

/**
 * @brief Get the latest sensor reading.
 * Thread-safe — may be called from any context.
 */
void monitor_get_latest(struct monitor_reading *out);

/**
 * @brief Trigger a high-priority alarm condition (simulates a threshold breach).
 *
 * Temporarily wakes the high-priority alarm thread, increasing scheduler
 * pressure so the CPU frequency policy scales up.  Call monitor_clear_alarm()
 * to return to normal.
 */
void monitor_trigger_alarm(void);

/** @brief Clear a previously triggered alarm condition. */
void monitor_clear_alarm(void);

#ifdef __cplusplus
}
#endif

#endif /* SECUREVAULT_MONITOR_H */
