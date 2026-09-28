/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Smart Sensor Hub — Ztest Benchmarks
 * ====================================
 *
 * Demonstrates the **Ztest Benchmark Framework** introduced in Zephyr 4.4.
 *
 * This companion application measures the cycle cost of key operations
 * used in the Smart Sensor Hub demo:
 *
 *   • Zbus channel publish
 *   • Zbus channel read
 *   • Scope-based cleanup (allocation + auto-free)
 *   • Raw struct copy (baseline)
 *
 * Build & run:
 *   west build -b native_sim samples/demo/smart_sensor_hub/benchmark
 *   west build -t run
 */

#include <zephyr/ztest.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/cleanup.h>

LOG_MODULE_REGISTER(benchmark, LOG_LEVEL_INF);

/* -------- Types matching the main application ---------------------------- */

struct sensor_reading {
	int32_t temperature_mcelsius;
	int32_t humidity_mpercent;
	int32_t pressure_mpa;
	uint32_t seq;
};

/* -------- Zbus channel for benchmarking ---------------------------------- */

ZBUS_CHAN_DEFINE(bench_chan,
		 struct sensor_reading,
		 NULL, NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.temperature_mcelsius = 0,
			       .humidity_mpercent = 0,
			       .pressure_mpa = 0,
			       .seq = 0)
);

/* -------- Scope-var for benchmarking cleanup overhead --------------------- */

struct cal_ctx {
	int32_t temp_offset;
	int32_t hum_offset;
	int32_t pres_offset;
};

static struct cal_ctx *cal_ctx_create(int32_t t, int32_t h, int32_t p)
{
	struct cal_ctx *ctx = k_malloc(sizeof(*ctx));

	if (ctx != NULL) {
		ctx->temp_offset = t;
		ctx->hum_offset  = h;
		ctx->pres_offset = p;
	}
	return ctx;
}

static void cal_ctx_destroy(struct cal_ctx *ctx)
{
	if (ctx != NULL) {
		k_free(ctx);
	}
}

SCOPE_VAR_DEFINE(cal, struct cal_ctx *, cal_ctx_destroy(_T),
		 cal_ctx_create(t_off, h_off, p_off),
		 int32_t t_off, int32_t h_off, int32_t p_off);

/* -------- Benchmark suite ------------------------------------------------ */

ZTEST_BENCHMARK_SUITE(sensor_hub_benchmarks, NULL, NULL);

/* Baseline: raw struct copy */
ZTEST_BENCHMARK(sensor_hub_benchmarks, struct_copy, 1000)
{
	struct sensor_reading src = {
		.temperature_mcelsius = 22000,
		.humidity_mpercent    = 45000,
		.pressure_mpa         = 101325,
		.seq                  = 42,
	};
	struct sensor_reading dst;

	dst = src;
	(void)dst;
}

/* Zbus: publish a sensor reading */
ZTEST_BENCHMARK(sensor_hub_benchmarks, zbus_publish, 1000)
{
	struct sensor_reading msg = {
		.temperature_mcelsius = 23500,
		.humidity_mpercent    = 50000,
		.pressure_mpa         = 101500,
		.seq                  = 99,
	};

	(void)zbus_chan_pub(&bench_chan, &msg, K_NO_WAIT);
}

/* Zbus: read from a channel */
ZTEST_BENCHMARK(sensor_hub_benchmarks, zbus_read, 1000)
{
	struct sensor_reading msg;

	(void)zbus_chan_read(&bench_chan, &msg, K_NO_WAIT);
}

/* Scope-var: allocate + auto-free via cleanup attribute */
ZTEST_BENCHMARK(sensor_hub_benchmarks, scope_var_alloc_free, 1000)
{
	scope_var(cal, ctx)(0, 0, 0);

	(void)ctx;
	/* ctx is freed automatically at scope exit */
}
