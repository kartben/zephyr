/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Smart Sensor Hub — Zephyr 4.4 Feature Showcase
 * ================================================
 *
 * A factory-floor environmental monitoring station that demonstrates several
 * headline features introduced in Zephyr 4.4:
 *
 *   • **Zbus** publish/subscribe messaging between decoupled modules
 *   • **Scope-based cleanup helpers** (scope_var, scope_guard) for
 *     automatic, exception-safe resource management in C
 *   • **Ztest benchmarking framework** (in the companion benchmark/ app)
 *
 * Architecture
 * ------------
 *
 *   sensor_sim  ──►  [ sensor_data_chan ]  ──►  alert_engine
 *                                                    │
 *                                                    ▼
 *                                             [ alert_chan ]
 *                                                    │
 *                                                    ▼
 *                                             alert_listener (log)
 *
 * The application runs entirely on native_sim — no hardware required.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "sensor_sim.h"
#include "alert_engine.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* -------- Zbus channel definitions (one place, shared via channels.h) ---- */

ZBUS_CHAN_DEFINE(sensor_data_chan,       /* Name */
		 struct sensor_reading,  /* Message type */
		 NULL,                   /* Validator */
		 NULL,                   /* User data */
		 ZBUS_OBSERVERS_EMPTY,   /* Observers added at compile-time via
					  * ZBUS_CHAN_ADD_OBS in each module
					  */
		 ZBUS_MSG_INIT(.temperature_mcelsius = 0,
			       .humidity_mpercent = 0,
			       .pressure_mpa = 0,
			       .seq = 0)
);

ZBUS_CHAN_DEFINE(alert_chan,              /* Name */
		 struct alert_event,     /* Message type */
		 NULL,                   /* Validator */
		 NULL,                   /* User data */
		 ZBUS_OBSERVERS_EMPTY,   /* Observers added at compile-time via
					  * ZBUS_CHAN_ADD_OBS in each module
					  */
		 ZBUS_MSG_INIT(.severity = 0,
			       .sensor_id = 0,
			       .value = 0,
			       .threshold = 0)
);

/* -------- Entry point ---------------------------------------------------- */

int main(void)
{
	LOG_INF("==========================================================");
	LOG_INF("  Smart Sensor Hub — Zephyr 4.4 Feature Showcase started");
	LOG_INF("==========================================================");
	LOG_INF("");
	LOG_INF("Features demonstrated:");
	LOG_INF("  [1] Zbus pub/sub messaging (sensor -> alert pipeline)");
	LOG_INF("  [2] Scope-based cleanup helpers (scope_var, scope_guard)");
	LOG_INF("  [3] Ztest benchmark framework (see benchmark/ sub-app)");
	LOG_INF("");

	sensor_sim_start();
	alert_engine_start();

	LOG_INF("All modules running. Watching for threshold breaches...");
	LOG_INF("");

	return 0;
}
