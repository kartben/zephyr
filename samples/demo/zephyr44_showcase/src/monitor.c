/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — environmental monitor module
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 feature showcased in this file                             │
 * │                                                                         │
 * │  CPU Frequency Scaling — pressure-based policy (experimental)          │
 * │  (subsys/cpu_freq/, include/zephyr/cpu_freq/)                          │
 * │                                                                         │
 * │  The pressure policy monitors how many threads at each priority level  │
 * │  are runnable.  When higher-priority threads become active the policy  │
 * │  raises the CPU P-state; when only low-priority work remains it scales │
 * │  back down.  No application code calls cpu_freq_* directly — the       │
 * │  policy runs autonomously in the background.  What the application     │
 * │  does control is the workload shape: which threads exist and at what   │
 * │  priority they run.                                                     │
 * │                                                                         │
 * │  Three threads model a real sensor pipeline:                           │
 * │    • alarm_thread   (prio 1)  — reacts to threshold breaches           │
 * │    • sample_thread  (prio 5)  — periodic sensor polling                │
 * │    • aggregate_thread (prio 10) — rolling average / history            │
 * │                                                                         │
 * │  During normal operation only sample + aggregate run → low pressure    │
 * │  → CPU stays at lowest P-state.  A threshold breach wakes alarm_thread │
 * │  → high pressure → CPU scales up to handle the burst workload.        │
 * └─────────────────────────────────────────────────────────────────────────┘
 */

#include "monitor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(monitor, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------- */

/* Latest reading, protected by a mutex. */
static struct monitor_reading latest;
static struct k_mutex          reading_mutex;

/* Alarm semaphore — alarm_thread blocks on this. */
static struct k_sem alarm_sem;

/* -------------------------------------------------------------------------
 * Simulated sensor — on real hardware replace with sensor_channel_get().
 * ------------------------------------------------------------------ */
static void read_sensor(int32_t *temp_mc, int32_t *hum_mpct)
{
	static int32_t t = 21000; /* start at 21.0 °C */
	static int32_t h = 45000; /* start at 45.0 %  */

	/* Simulate slow drift with wraparound so the demo never stalls. */
	t += (sys_rand32_get() % 300) - 100; /* ±0.1 °C steps */
	h += (sys_rand32_get() % 200) - 100; /* ±0.1 % steps  */

	t = CLAMP(t, 15000, 35000);
	h = CLAMP(h, 20000, 80000);

	*temp_mc  = t;
	*hum_mpct = h;
}

/* -------------------------------------------------------------------------
 * Thread: alarm_thread  (priority 1 — highest pressure contributor)
 *
 * Normally blocked waiting for the alarm semaphore.  When a threshold
 * breach is signalled it becomes runnable, adding high-priority pressure
 * to the scheduler queue.  The CPU freq pressure policy sees this and
 * scales up the P-state within one evaluation interval.
 * ---------------------------------------------------------------------- */
#define ALARM_STACK_SIZE 512
K_THREAD_STACK_DEFINE(alarm_stack, ALARM_STACK_SIZE);
static struct k_thread alarm_thread_data;

static void alarm_thread(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	while (1) {
		/*
		 * Block here most of the time — zero scheduler pressure.
		 * When monitor_trigger_alarm() gives the semaphore this thread
		 * wakes up, becomes runnable at prio 1, and the pressure policy
		 * sees a high-priority thread in the ready queue.
		 */
		k_sem_take(&alarm_sem, K_FOREVER);

		LOG_WRN("ALARM: sensor threshold breached — CPU freq scaled UP");

		/* Simulate doing meaningful work (alert dispatch, logging…). */
		k_busy_wait(50000); /* 50 ms of work */
	}
}

/* -------------------------------------------------------------------------
 * Thread: sample_thread  (priority 5 — medium pressure)
 *
 * Polls the sensor every second.  This is the "always-on" workload that
 * keeps a moderate P-state selected during normal operation.
 * ---------------------------------------------------------------------- */
#define SAMPLE_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(sample_stack, SAMPLE_STACK_SIZE);
static struct k_thread sample_thread_data;

static void sample_thread(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

	uint32_t seq = 0;

	while (1) {
		int32_t temp, hum;

		read_sensor(&temp, &hum);

		k_mutex_lock(&reading_mutex, K_FOREVER);
		latest.temp_mc  = temp;
		latest.hum_mpct = hum;
		latest.seq      = ++seq;
		k_mutex_unlock(&reading_mutex);

		LOG_DBG("Sample #%u: temp=%d.%03d°C hum=%d.%03d%%", seq,
			temp / 1000, temp % 1000, hum / 1000, hum % 1000);

		/* Check thresholds — trigger alarm if temp > 30 °C. */
		if (temp > 30000) {
			k_sem_give(&alarm_sem);
		}

		k_sleep(K_SECONDS(1));
	}
}

/* -------------------------------------------------------------------------
 * Thread: aggregate_thread  (priority 10 — lowest pressure)
 *
 * Computes a 10-sample rolling average for reporting.  This is background
 * bookkeeping work — low priority, runs only when the system is idle.
 * When only this thread is runnable the pressure policy allows the CPU
 * to drop to its lowest P-state.
 * ---------------------------------------------------------------------- */
#define AGGREGATE_STACK_SIZE 1024
K_THREAD_STACK_DEFINE(aggregate_stack, AGGREGATE_STACK_SIZE);
static struct k_thread aggregate_thread_data;

static void aggregate_thread(void *a1, void *a2, void *a3)
{
	ARG_UNUSED(a1);
	ARG_UNUSED(a2);
	ARG_UNUSED(a3);

#define HISTORY_LEN 10
	int32_t temp_hist[HISTORY_LEN] = {0};
	int32_t hum_hist[HISTORY_LEN]  = {0};
	uint32_t last_seq = 0;
	int idx = 0;

	while (1) {
		struct monitor_reading r;

		monitor_get_latest(&r);

		if (r.seq != last_seq) {
			last_seq = r.seq;
			temp_hist[idx % HISTORY_LEN] = r.temp_mc;
			hum_hist[idx % HISTORY_LEN]  = r.hum_mpct;
			idx++;
		}

		/* Slow background work — this keeps the thread runnable but
		 * the long sleep means it doesn't *block* other threads. */
		k_sleep(K_SECONDS(5));
	}
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int monitor_init(void)
{
	k_mutex_init(&reading_mutex);
	k_sem_init(&alarm_sem, 0, 10);

	/* alarm_thread starts suspended; monitor_trigger_alarm() wakes it. */
	k_thread_create(&alarm_thread_data, alarm_stack, ALARM_STACK_SIZE,
			alarm_thread, NULL, NULL, NULL,
			1, 0, K_NO_WAIT);
	k_thread_name_set(&alarm_thread_data, "sv_alarm");

	k_thread_create(&sample_thread_data, sample_stack, SAMPLE_STACK_SIZE,
			sample_thread, NULL, NULL, NULL,
			5, 0, K_NO_WAIT);
	k_thread_name_set(&sample_thread_data, "sv_sample");

	k_thread_create(&aggregate_thread_data, aggregate_stack, AGGREGATE_STACK_SIZE,
			aggregate_thread, NULL, NULL, NULL,
			10, 0, K_NO_WAIT);
	k_thread_name_set(&aggregate_thread_data, "sv_aggregate");

	LOG_INF("Monitor started — CPU freq pressure policy will track prio 1..10");

	return 0;
}

void monitor_get_latest(struct monitor_reading *out)
{
	k_mutex_lock(&reading_mutex, K_FOREVER);
	*out = latest;
	k_mutex_unlock(&reading_mutex);
}

void monitor_trigger_alarm(void)
{
	LOG_WRN("Alarm triggered (simulated threshold breach)");
	k_sem_give(&alarm_sem);
}

void monitor_clear_alarm(void)
{
	/* Drain any pending semaphore tokens — alarm_thread will block again. */
	while (k_sem_take(&alarm_sem, K_NO_WAIT) == 0) {
		;
	}
	LOG_INF("Alarm cleared");
}
