/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — application entry point
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 Feature Showcase — "SecureVault Node"                      │
 * │                                                                         │
 * │  Business scenario                                                      │
 * │  ─────────────────                                                      │
 * │  A compact IoT node deployed in sensitive facilities (data centres,    │
 * │  pharmaceutical clean rooms, industrial control rooms).  Each node:    │
 * │                                                                         │
 * │    • Authenticates field engineers via fingerprint before allowing      │
 * │      any local reconfiguration.                                         │
 * │    • Continuously monitors temperature and humidity.                    │
 * │    • Transmits telemetry over an encrypted WireGuard VPN tunnel.       │
 * │    • Adapts CPU frequency to workload — quiet when idle, fast when     │
 * │      processing alarms.                                                 │
 * │    • Exposes a local shell for technicians using shell_readline() for   │
 * │      interactive confirmation prompts.                                  │
 * │                                                                         │
 * │  Features demonstrated (all new in Zephyr 4.4)                         │
 * │  ──────────────────────────────────────────────                         │
 * │  ① Biometrics API          auth.c                                      │
 * │  ② Scope-based Cleanup     auth.c, report.c                            │
 * │  ③ WireGuard VPN           report.c                                    │
 * │  ④ CPU Freq Pressure Policy monitor.c                                  │
 * │  ⑤ shell_readline()        shell.c                                     │
 * │  ⑥ ztest Benchmark         tests/benchmark/                            │
 * └─────────────────────────────────────────────────────────────────────────┘
 */

#include "auth.h"
#include "monitor.h"
#include "report.h"

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

/* How often to send a telemetry report. */
#define REPORT_INTERVAL_SEC 10

/* Track authentication events between reports. */
static uint32_t auth_event_count;

/* -------------------------------------------------------------------------
 * Reporting work item — fires every REPORT_INTERVAL_SEC seconds.
 * ---------------------------------------------------------------------- */
static void report_work_fn(struct k_work *work);
K_WORK_DELAYABLE_DEFINE(report_work, report_work_fn);

static void report_work_fn(struct k_work *work)
{
	struct monitor_reading r;

	monitor_get_latest(&r);
	report_send(&r, auth_event_count);
	auth_event_count = 0;

	k_work_reschedule(&report_work, K_SECONDS(REPORT_INTERVAL_SEC));
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */
int main(void)
{
	int ret;

	LOG_INF("========================================");
	LOG_INF(" SecureVault Node — Zephyr 4.4 Showcase");
	LOG_INF("========================================");

	/* ----------------------------------------------------------------
	 * ① Biometrics API — initialise the fingerprint sensor.
	 *    On native_sim this binds to the emulated driver from
	 *    boards/native_sim.overlay.
	 * ------------------------------------------------------------- */
	ret = auth_init();
	if (ret < 0) {
		LOG_ERR("auth_init failed: %d — halting", ret);
		return ret;
	}

	/* ----------------------------------------------------------------
	 * ③ WireGuard — set up the VPN tunnel.
	 *    Skipped gracefully when CONFIG_WIREGUARD is not set.
	 * ------------------------------------------------------------- */
	ret = report_init();
	if (ret < 0) {
		LOG_WRN("report_init returned %d — continuing without VPN", ret);
	}

	/* ----------------------------------------------------------------
	 * ④ CPU Frequency Pressure Policy — start the sensor pipeline.
	 *    Three threads at priorities 1, 5, 10 create varying scheduler
	 *    pressure.  The pressure policy (CONFIG_CPU_FREQ_POLICY_PRESSURE)
	 *    raises the CPU P-state when high-priority threads are runnable
	 *    and lowers it when only low-priority work remains.
	 * ------------------------------------------------------------- */
	ret = monitor_init();
	if (ret < 0) {
		LOG_ERR("monitor_init failed: %d", ret);
		return ret;
	}

	/* ----------------------------------------------------------------
	 * Enroll a demo user if the template database is empty.
	 * In production this would be done through the shell after
	 * physical installation.
	 * ------------------------------------------------------------- */
	if (auth_enrolled_count() == 0) {
		LOG_INF("No users enrolled — auto-enrolling demo user 1");
		ret = auth_enroll(1, K_SECONDS(10));
		if (ret == 0) {
			LOG_INF("Demo user 1 enrolled — try: securevault verify 1");
		} else {
			LOG_WRN("Auto-enroll failed (%d); use 'securevault enroll 1'", ret);
		}
	} else {
		LOG_INF("%zu user(s) already enrolled", auth_enrolled_count());
	}

	/* ----------------------------------------------------------------
	 * Start periodic telemetry reporting.
	 * ⑤ shell_readline() is available via 'securevault' shell commands.
	 * ---------------------------------------------------------------- */
	LOG_INF("Starting telemetry reporting every %d seconds", REPORT_INTERVAL_SEC);
	k_work_schedule(&report_work, K_SECONDS(REPORT_INTERVAL_SEC));

	LOG_INF("");
	LOG_INF("Shell ready. Try:");
	LOG_INF("  securevault status");
	LOG_INF("  securevault enroll 2");
	LOG_INF("  securevault identify");
	LOG_INF("  securevault alarm");

	/*
	 * Main thread exits here — the shell, monitor threads, and report
	 * work item keep running.  The CPU freq pressure policy continues to
	 * evaluate the thread ready queue every CONFIG_CPU_FREQ_INTERVAL_MS.
	 */
	return 0;
}
