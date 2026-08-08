/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zenbedded/control.h>
#include <zenbedded/hist.h>

/*
 * Unlike the tests in tests/control, this one starts the real 1 kHz thread and
 * measures what it actually does.
 *
 * One caveat, stated here rather than discovered later: under native_sim the
 * cycle counter reads *simulated* time, so this measures scheduling correctness
 * rather than host jitter. What it genuinely catches is the loop running at the
 * wrong rate, drifting, or missing deadlines -- including the tick-granularity
 * mistake where k_timer quantises a 1 ms period up to a 10 ms tick and nothing
 * complains. The jitter numbers that go on a slide come from hardware, in
 * Phase 4.
 */

#define ZB_RUN_MS 2000
#define ZB_PERIOD CONFIG_ZENBEDDED_CONTROL_PERIOD_US

static void *timing_setup(void)
{
	zb_control_start();
	k_sleep(K_MSEC(ZB_RUN_MS));

	return NULL;
}

ZTEST_SUITE(zb_loop_timing, NULL, timing_setup, NULL, NULL, NULL);

ZTEST(zb_loop_timing, test_loop_runs_at_the_configured_rate)
{
	const struct zb_hist *h = &zb_loop_hist;
	uint32_t expected = (ZB_RUN_MS * 1000U) / ZB_PERIOD;

	TC_PRINT("loop: n=%u p50=%uus p99=%uus p99.9=%uus max=%uus min=%uus overruns=%u\n",
		 h->count, zb_hist_percentile(h, 500U), zb_hist_percentile(h, 990U),
		 zb_hist_percentile(h, 999U), h->max_us, h->min_us, h->overruns);

	zassert_true(h->count > 0U, "the control loop never ran");

	/*
	 * Within 5%. A loop quantised to a coarser tick lands at a fraction of
	 * this and fails loudly, which is the point.
	 */
	zassert_within((double)h->count, (double)expected, (double)expected * 0.05,
		       "loop ran %u times in %d ms, expected about %u: check "
		       "CONFIG_SYS_CLOCK_TICKS_PER_SEC against the control period",
		       h->count, ZB_RUN_MS, expected);
}

ZTEST(zb_loop_timing, test_no_overruns)
{
	const struct zb_hist *h = &zb_loop_hist;

	zassert_equal(h->overruns, 0U, "%u missed control deadlines", h->overruns);
}

ZTEST(zb_loop_timing, test_period_holds)
{
	const struct zb_hist *h = &zb_loop_hist;

	uint32_t p50 = zb_hist_percentile(h, 500U);
	uint32_t p99 = zb_hist_percentile(h, 990U);
	uint32_t p999 = zb_hist_percentile(h, 999U);

	/*
	 * Reported and asserted together. A p99 that holds while max is off in
	 * the weeds is a different failure from a loop whose median has drifted,
	 * and quoting only one of them hides whichever is worse.
	 */
	zassert_true(p50 <= (uint32_t)(ZB_PERIOD * 1.05f), "p50 %u us drifted off %d us", p50,
		     ZB_PERIOD);
	zassert_true(p99 <= (uint32_t)(ZB_PERIOD * 1.2f), "p99 %u us", p99);
	zassert_true(p999 <= (uint32_t)(ZB_PERIOD * 1.5f), "p99.9 %u us", p999);
	zassert_true(h->min_us >= (uint32_t)(ZB_PERIOD * 0.5f), "min %u us: the loop is early",
		     h->min_us);
}

ZTEST(zb_loop_timing, test_joint_is_parked_with_no_host)
{
	/*
	 * Nothing published a setpoint for two seconds of real running, so the
	 * failover machine should have taken the joint all the way down on its
	 * own, with no test scaffolding involved.
	 */
	zassert_equal(zb_rt[0].mode, ZB_MODE_PARK, "joint should have parked itself");
	zassert_false(zb_rt[0].driver_enabled, "and cut the driver");
}
