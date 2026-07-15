/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Smart Sensor Hub — Alert Engine
 * ================================
 *
 * Subscribes to sensor_data_chan via zbus, evaluates safety thresholds, and
 * publishes alert_event messages when breached.  Demonstrates:
 *
 *   1. **Zbus subscriber** — blocking wait on channel updates.
 *   2. **Zbus listener** — synchronous callback for lightweight reactions.
 *   3. **Scope-based guard** — automatic mutex lock/unlock using
 *      `scope_guard` so the critical section is always exited, even on
 *      error paths (Zephyr 4.4 feature).
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/cleanup.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "alert_engine.h"

LOG_MODULE_REGISTER(alert_engine, LOG_LEVEL_INF);

/* ---------- Thresholds --------------------------------------------------- */

#define TEMP_WARN_MC   26000   /* 26 °C — warning */
#define TEMP_CRIT_MC   29000   /* 29 °C — critical */
#define HUM_WARN_MP    55000   /* 55 %  — warning */
#define HUM_CRIT_MP    58000   /* 58 %  — critical */

/* Severity levels — keep in sync with struct alert_event.severity */
#define SEV_INFO     0
#define SEV_WARNING  1
#define SEV_CRITICAL 2

/* ---------- Alert statistics (guarded by a mutex) ------------------------ */

static struct {
	uint32_t warnings;
	uint32_t criticals;
	uint32_t total_readings;
} alert_stats;

static K_MUTEX_DEFINE(stats_mutex);

/* ---------- Zephyr 4.4 Feature: scope_guard for mutex -------------------- */

/*
 * SCOPE_GUARD_DEFINE creates a guard type that acquires a lock on
 * initialisation and releases it when the variable goes out of scope.
 * Using `scope_guard(k_mutex)(&stats_mutex)` in a function body means
 * the mutex is *always* unlocked — no matter how the function returns.
 */
SCOPE_GUARD_DEFINE(k_mutex, struct k_mutex *,
		   (void)k_mutex_lock(_T, K_FOREVER),
		   (void)k_mutex_unlock(_T));

/**
 * Update statistics safely using a scope-guarded mutex.
 *
 * The `scope_guard(k_mutex)` call locks the mutex and automatically unlocks
 * it when this function returns — even on early exit.
 */
static void record_alert(uint8_t severity)
{
	scope_guard(k_mutex)(&stats_mutex);

	/* Mutex is held from this point until scope exit. */

	if (severity == SEV_WARNING) {
		alert_stats.warnings++;
	} else if (severity == SEV_CRITICAL) {
		alert_stats.criticals++;
	}
}

static void record_reading(void)
{
	scope_guard(k_mutex)(&stats_mutex);

	alert_stats.total_readings++;
}

/* ---------- Threshold evaluation ----------------------------------------- */

static void check_and_alert(const struct sensor_reading *reading)
{
	struct alert_event evt = {0};

	record_reading();

	/* Temperature checks */
	if (reading->temperature_mcelsius >= TEMP_CRIT_MC) {
		evt.severity  = SEV_CRITICAL;
		evt.sensor_id = 0; /* temperature */
		evt.value     = reading->temperature_mcelsius;
		evt.threshold = TEMP_CRIT_MC;

		LOG_ERR("ALERT [CRITICAL] Temperature %d.%03d°C >= %d.%03d°C",
			reading->temperature_mcelsius / 1000,
			reading->temperature_mcelsius % 1000,
			TEMP_CRIT_MC / 1000, TEMP_CRIT_MC % 1000);

		record_alert(SEV_CRITICAL);
		zbus_chan_pub(&alert_chan, &evt, K_MSEC(200));
	} else if (reading->temperature_mcelsius >= TEMP_WARN_MC) {
		evt.severity  = SEV_WARNING;
		evt.sensor_id = 0;
		evt.value     = reading->temperature_mcelsius;
		evt.threshold = TEMP_WARN_MC;

		LOG_WRN("ALERT [WARNING]  Temperature %d.%03d°C >= %d.%03d°C",
			reading->temperature_mcelsius / 1000,
			reading->temperature_mcelsius % 1000,
			TEMP_WARN_MC / 1000, TEMP_WARN_MC % 1000);

		record_alert(SEV_WARNING);
		zbus_chan_pub(&alert_chan, &evt, K_MSEC(200));
	}

	/* Humidity checks */
	if (reading->humidity_mpercent >= HUM_CRIT_MP) {
		evt.severity  = SEV_CRITICAL;
		evt.sensor_id = 1; /* humidity */
		evt.value     = reading->humidity_mpercent;
		evt.threshold = HUM_CRIT_MP;

		LOG_ERR("ALERT [CRITICAL] Humidity %d.%03d%% >= %d.%03d%%",
			reading->humidity_mpercent / 1000,
			reading->humidity_mpercent % 1000,
			HUM_CRIT_MP / 1000, HUM_CRIT_MP % 1000);

		record_alert(SEV_CRITICAL);
		zbus_chan_pub(&alert_chan, &evt, K_MSEC(200));
	} else if (reading->humidity_mpercent >= HUM_WARN_MP) {
		evt.severity  = SEV_WARNING;
		evt.sensor_id = 1;
		evt.value     = reading->humidity_mpercent;
		evt.threshold = HUM_WARN_MP;

		LOG_WRN("ALERT [WARNING]  Humidity %d.%03d%% >= %d.%03d%%",
			reading->humidity_mpercent / 1000,
			reading->humidity_mpercent % 1000,
			HUM_WARN_MP / 1000, HUM_WARN_MP % 1000);

		record_alert(SEV_WARNING);
		zbus_chan_pub(&alert_chan, &evt, K_MSEC(200));
	}
}

/* ---------- Zbus subscriber (blocking wait in dedicated thread) ---------- */

ZBUS_SUBSCRIBER_DEFINE(alert_sub, 4);
ZBUS_CHAN_ADD_OBS(sensor_data_chan, alert_sub, 0);

static void alert_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct zbus_channel *chan;

	LOG_INF("Alert engine thread started — monitoring thresholds");

	while (!zbus_sub_wait(&alert_sub, &chan, K_FOREVER)) {
		if (chan == &sensor_data_chan) {
			struct sensor_reading reading;

			zbus_chan_read(&sensor_data_chan, &reading, K_MSEC(500));
			check_and_alert(&reading);
		}
	}
}

K_THREAD_DEFINE(alert_engine_tid, 1024, alert_thread_fn,
		NULL, NULL, NULL, 4, 0, 0);

/* ---------- Zbus listener for alert logging ------------------------------ */

static void alert_listener_cb(const struct zbus_channel *chan)
{
	const struct alert_event *evt = zbus_chan_const_msg(chan);
	const char *sev_str = (evt->severity == SEV_CRITICAL) ? "CRITICAL"
			    : (evt->severity == SEV_WARNING)  ? "WARNING"
							      : "INFO";

	LOG_INF(">>> Alert dispatched: severity=%s sensor=%u value=%d threshold=%d",
		sev_str, evt->sensor_id, evt->value, evt->threshold);
}

ZBUS_LISTENER_DEFINE(alert_listener, alert_listener_cb);
ZBUS_CHAN_ADD_OBS(alert_chan, alert_listener, 0);

/* ---------- Periodic statistics report ----------------------------------- */

static void stats_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	scope_guard(k_mutex)(&stats_mutex);

	LOG_INF("--- Stats: %u readings | %u warnings | %u criticals ---",
		alert_stats.total_readings,
		alert_stats.warnings,
		alert_stats.criticals);
}

K_TIMER_DEFINE(stats_timer, stats_timer_handler, NULL);

void alert_engine_start(void)
{
	/* Print stats every 10 seconds. */
	k_timer_start(&stats_timer, K_SECONDS(10), K_SECONDS(10));
	LOG_INF("Alert engine module initialised");
}
