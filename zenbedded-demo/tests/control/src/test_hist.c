/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include <zenbedded/hist.h>

#include "test_common.h"

static struct zb_hist h;

static void hist_before(void *f)
{
	ARG_UNUSED(f);
	zb_hist_reset(&h);
}

ZTEST_SUITE(zb_hist, NULL, NULL, hist_before, NULL, NULL);

ZTEST(zb_hist, test_empty)
{
	zassert_equal(h.count, 0U);
	zassert_equal(h.overruns, 0U);
	zassert_equal(zb_hist_percentile(&h, 500U), 0U, "an empty histogram has no p50");
}

ZTEST(zb_hist, test_buckets_derive_from_configured_period)
{
	/*
	 * Edges are ratios to the control period, not absolute microseconds, so
	 * retuning the loop rate moves them with it.
	 */
	uint32_t period = CONFIG_ZENBEDDED_CONTROL_PERIOD_US;

	zassert_equal(zb_hist_bucket_edge_us(0), period / 2U, "first edge is 0.5x period");
	zassert_equal(zb_hist_bucket_edge_us(5), period, "the target period is an edge");
	zassert_equal(zb_hist_bucket_edge_us(ZB_HIST_BUCKETS - 1), UINT32_MAX,
		      "the top bucket is open-ended");

	/* Edges must be strictly increasing or percentiles are meaningless. */
	for (uint32_t i = 1; i < ZB_HIST_BUCKETS; i++) {
		zassert_true(zb_hist_bucket_edge_us(i) > zb_hist_bucket_edge_us(i - 1),
			     "bucket edges must increase, broke at %u", i);
	}
}

ZTEST(zb_hist, test_min_max_and_count)
{
	zb_hist_record(&h, 990U);
	zb_hist_record(&h, 1000U);
	zb_hist_record(&h, 1400U);

	zassert_equal(h.count, 3U);
	zassert_equal(h.min_us, 990U);
	zassert_equal(h.max_us, 1400U);
	zassert_equal(h.sum_us, 3390U);
}

ZTEST(zb_hist, test_percentiles_are_conservative)
{
	/* 99 samples on target, one far out in the tail. */
	for (int i = 0; i < 99; i++) {
		zb_hist_record(&h, 1000U);
	}
	zb_hist_record(&h, 9000U);

	uint32_t p50 = zb_hist_percentile(&h, 500U);
	uint32_t p99 = zb_hist_percentile(&h, 990U);
	uint32_t max = h.max_us;

	/* 1000 lands in the [1000, 1020) bucket, whose upper edge is quoted. */
	zassert_equal(p50, 1020U);
	zassert_equal(p99, 1020U, "the single outlier must not move p99");
	zassert_equal(max, 9000U, "but it is visible in max");
	zassert_true(p99 <= max, "percentiles never exceed max");

	/*
	 * The point of reporting these together: p99 and max disagree by almost
	 * 9x here, and either one quoted alone tells a different story.
	 */
	zassert_true(zb_hist_percentile(&h, 999U) >= p99, "p99.9 must not fall below p99");
}

ZTEST(zb_hist, test_overruns_counted_separately)
{
	zb_hist_record(&h, 2000U);
	zb_hist_record_overrun(&h, 1U);
	zb_hist_record_overrun(&h, 3U);

	zassert_equal(h.overruns, 4U);
	zassert_equal(h.count, 1U, "overruns are not samples");
}
