/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Smart Sensor Hub — Zephyr 4.4 Feature Showcase
 * ================================================
 *
 * Simulated sensor module that publishes environmental readings on a
 * zbus channel.  Demonstrates two Zephyr 4.4 features:
 *
 *   1. **Zbus pub/sub** — decoupled, type-safe data flow between modules.
 *   2. **Scope-based cleanup helpers** — RAII / defer-style automatic
 *      resource management using `scope_var` and `SCOPE_VAR_DEFINE`.
 *
 * The cleanup helpers ensure that a dynamically-allocated calibration
 * context is always freed, even on early-return error paths — a pattern
 * that eliminates an entire class of resource-leak bugs in embedded C.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/cleanup.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "sensor_sim.h"

LOG_MODULE_REGISTER(sensor_sim, LOG_LEVEL_INF);

/* ---------- Zephyr 4.4 Feature: scope-based cleanup ----------------------
 *
 * We define a "calibration context" that is heap-allocated and must be freed
 * after every reading cycle.  `SCOPE_VAR_DEFINE` + `scope_var` guarantee
 * automatic cleanup — even if the function returns early due to an error.
 */

/** Opaque calibration context — allocated per reading cycle. */
struct cal_ctx {
	int32_t temp_offset;
	int32_t hum_offset;
	int32_t pres_offset;
};

static struct cal_ctx *cal_ctx_create(int32_t t_off, int32_t h_off, int32_t p_off)
{
	struct cal_ctx *ctx = k_malloc(sizeof(*ctx));

	if (ctx != NULL) {
		ctx->temp_offset = t_off;
		ctx->hum_offset  = h_off;
		ctx->pres_offset = p_off;
	}
	return ctx;
}

static void cal_ctx_destroy(struct cal_ctx *ctx)
{
	if (ctx != NULL) {
		LOG_DBG("  [scope cleanup] freeing calibration context %p", ctx);
		k_free(ctx);
	}
}

/*
 * SCOPE_VAR_DEFINE ties the type, destructor and constructor together so that
 * a variable declared with `scope_var(cal, name)(args)` is automatically
 * destroyed when the enclosing scope exits.
 */
SCOPE_VAR_DEFINE(cal, struct cal_ctx *, cal_ctx_destroy(_T),
		 cal_ctx_create(t_off, h_off, p_off),
		 int32_t t_off, int32_t h_off, int32_t p_off);

/* ---------- Simulated sensor baseline + drift ---------------------------- */

#define BASE_TEMP_MC    22000   /* 22.000 °C */
#define BASE_HUM_MP     45000   /* 45.000 %  */
#define BASE_PRES_MPA   101325  /* 101.325 kPa — stored in milli-units to keep
				 * the demo integer-only; the field name in the
				 * struct is `pressure_mpa` (milli-Pa).
				 */

/** Simple deterministic "noise" that varies with the sequence counter. */
static int32_t pseudo_drift(uint32_t seq, int32_t amplitude)
{
	/* Triangular wave: ramps up for 10 samples, then back down. */
	int32_t phase = seq % 20;

	if (phase >= 10) {
		phase = 20 - phase;
	}
	return (amplitude * phase) / 10;
}

/* ---------- Reading cycle ------------------------------------------------ */

/**
 * Perform one sensor reading cycle.
 *
 * The calibration context is allocated with `scope_var` so it is freed
 * automatically at scope exit — no explicit `k_free()` needed, even if we
 * add early-return error handling later.
 */
static int sensor_read_cycle(uint32_t seq, struct sensor_reading *out)
{
	/* scope_var(cal, ctx)(args) — allocates AND registers cleanup. */
	scope_var(cal, ctx)(0, 0, 0);

	if (ctx == NULL) {
		LOG_ERR("Failed to allocate calibration context");
		return -ENOMEM;
		/* ctx is NULL so cal_ctx_destroy is a safe no-op. */
	}

	/* Apply simulated drift + calibration offsets. */
	out->temperature_mcelsius = BASE_TEMP_MC
				  + pseudo_drift(seq, 8000)
				  + ctx->temp_offset;

	out->humidity_mpercent    = BASE_HUM_MP
				  + pseudo_drift(seq + 5, 15000)
				  + ctx->hum_offset;

	out->pressure_mpa         = BASE_PRES_MPA
				  + pseudo_drift(seq + 3, 500)
				  + ctx->pres_offset;

	out->seq = seq;

	/* ctx is freed here automatically by scope_var cleanup. */
	return 0;
}

/* ---------- Sensor thread ------------------------------------------------ */

#define SENSOR_INTERVAL_MS 2000

static void sensor_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint32_t seq = 0;

	LOG_INF("Sensor simulation thread started (interval %d ms)", SENSOR_INTERVAL_MS);

	while (true) {
		struct sensor_reading reading;
		int ret = sensor_read_cycle(seq, &reading);

		if (ret == 0) {
			LOG_INF("Sensor [#%u] T=%d.%03d°C  H=%d.%03d%%  P=%d.%03d kPa",
				reading.seq,
				reading.temperature_mcelsius / 1000,
				reading.temperature_mcelsius % 1000,
				reading.humidity_mpercent / 1000,
				reading.humidity_mpercent % 1000,
				reading.pressure_mpa / 1000,
				reading.pressure_mpa % 1000);

			/* Publish on zbus — any subscriber/listener picks it up. */
			ret = zbus_chan_pub(&sensor_data_chan, &reading, K_MSEC(500));
			if (ret < 0) {
				LOG_ERR("Failed to publish sensor data: %d", ret);
			}
		}

		seq++;
		k_msleep(SENSOR_INTERVAL_MS);
	}
}

K_THREAD_DEFINE(sensor_sim_tid, 1024, sensor_thread_fn,
		NULL, NULL, NULL, 5, 0, 0);

void sensor_sim_start(void)
{
	/* Thread is auto-started by K_THREAD_DEFINE — this function is a
	 * placeholder for any future one-time init.
	 */
	LOG_INF("Sensor simulation module initialised");
}
