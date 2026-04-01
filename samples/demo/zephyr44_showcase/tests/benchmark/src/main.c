/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — biometric latency benchmarks
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 feature showcased in this file                             │
 * │                                                                         │
 * │  ztest Benchmarking Framework                                          │
 * │  (subsys/testsuite/ztest/include/zephyr/benchmark.h)                  │
 * │                                                                         │
 * │  New in 4.4: statistical micro-benchmarking built into the ztest       │
 * │  framework.  Each benchmark:                                           │
 * │    • Measures CPU cycles per iteration via hardware cycle counter.     │
 * │    • Computes mean + variance (Welford's online algorithm), min, max.  │
 * │    • Outputs a summary table — optionally in CSV for CI dashboards.   │
 * │                                                                         │
 * │  Why this matters: embedded systems have hard real-time requirements.  │
 * │  "Authentication must complete in < 200 ms" is a spec, not a wish.    │
 * │  This benchmark turns that requirement into a CI-checkable number.     │
 * │                                                                         │
 * │  Usage pattern (the key thing to show in the demo):                    │
 * │                                                                         │
 * │    ZTEST_BENCHMARK_SUITE(suite_name, setup_fn, teardown_fn);           │
 * │    ZTEST_BENCHMARK(suite_name, benchmark_name, N_iterations)           │
 * │    {                                                                    │
 * │        // body runs N times, each iteration timed in cycles            │
 * │    }                                                                    │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Build & run:
 *   west build -b native_sim samples/demo/zephyr44_showcase/tests/benchmark
 *   west build -t run
 */

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/*
 * [4.4 NEW] Include the ztest benchmark framework.
 * On older Zephyr you had to roll your own timing loops.
 */
#include <zephyr/benchmark.h>

#define FINGERPRINT_NODE DT_ALIAS(fingerprint)

#if !DT_NODE_EXISTS(FINGERPRINT_NODE)
#error "No 'fingerprint' alias — add boards/native_sim.overlay to the build"
#endif

static const struct device *const fp = DEVICE_DT_GET(FINGERPRINT_NODE);

/* -------------------------------------------------------------------------
 * Suite setup / teardown
 *
 * The setup function runs once before all benchmarks in the suite.
 * It enrolls template ID 1 so verify/identify benchmarks have a match.
 * ---------------------------------------------------------------------- */
static void suite_setup(void)
{
	int ret;

	zassert_true(device_is_ready(fp), "Fingerprint device not ready");

	ret = biometric_enroll_start(fp, 1);
	zassert_ok(ret, "enroll_start failed: %d", ret);

	struct biometric_capabilities caps;

	biometric_get_capabilities(fp, &caps);

	for (int i = 0; i < caps.enrollment_samples_required; i++) {
		ret = biometric_enroll_capture(fp, K_SECONDS(2), NULL);
		zassert_ok(ret, "enroll_capture %d failed: %d", i, ret);
	}

	ret = biometric_enroll_finalize(fp);
	zassert_ok(ret, "enroll_finalize failed: %d", ret);
}

static void suite_teardown(void)
{
	biometric_template_delete_all(fp);
}

/* -------------------------------------------------------------------------
 * [4.4 NEW] Register benchmark suite.
 *
 * ZTEST_BENCHMARK_SUITE(name, setup_fn, teardown_fn)
 * ---------------------------------------------------------------------- */
ZTEST_BENCHMARK_SUITE(biometric, suite_setup, suite_teardown);

/* -------------------------------------------------------------------------
 * [4.4 NEW] Benchmark: biometric_match — VERIFY mode
 *
 * ZTEST_BENCHMARK(suite, name, N) expands to a function definition header.
 * The body (the block below) is what gets timed N times.
 *
 * Measures the critical path for "access granted":
 *   capture → match against known template → return result.
 * Must be < 200 ms per the SecureVault spec.
 * ---------------------------------------------------------------------- */
ZTEST_BENCHMARK(biometric, match_verify, 100)
{
	struct biometric_match_result result;

	biometric_match(fp, BIOMETRIC_MATCH_VERIFY, 1, K_SECONDS(5), &result);
}

/* -------------------------------------------------------------------------
 * [4.4 NEW] Benchmark: biometric_match — IDENTIFY mode
 *
 * Searches the entire template database rather than verifying a specific ID.
 * Latency scales with database size; this establishes the 1-template baseline.
 * ---------------------------------------------------------------------- */
ZTEST_BENCHMARK(biometric, match_identify, 100)
{
	struct biometric_match_result result;

	biometric_match(fp, BIOMETRIC_MATCH_IDENTIFY, 0, K_SECONDS(5), &result);
}

/* -------------------------------------------------------------------------
 * Benchmark: get_capabilities — overhead baseline
 *
 * A call that does no sensor I/O.  Establishes the driver dispatch overhead
 * so heavier benchmarks can be interpreted relative to it.
 * ---------------------------------------------------------------------- */
ZTEST_BENCHMARK(biometric, get_capabilities, 500)
{
	struct biometric_capabilities caps;

	biometric_get_capabilities(fp, &caps);
}

/* -------------------------------------------------------------------------
 * Benchmark: attr_get — metadata read after match
 *
 * Typical post-match pattern: read the image quality score.
 * Shows attribute access overhead.
 * ---------------------------------------------------------------------- */
ZTEST_BENCHMARK(biometric, attr_get_quality, 500)
{
	int32_t quality;

	biometric_attr_get(fp, BIOMETRIC_ATTR_IMAGE_QUALITY, &quality);
}
