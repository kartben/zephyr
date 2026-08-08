/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZENBEDDED_HIST_H_
#define ZENBEDDED_HIST_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of loop-period buckets. */
#define ZB_HIST_BUCKETS 16

/**
 * @brief Fixed-size loop-period histogram.
 *
 * Binned on-device. Publishing a timestamp per sample at 1 kHz would inject
 * exactly the jitter we are trying to measure, and someone at the booth will
 * say so.
 *
 * Bucket edges are derived from CONFIG_ZENBEDDED_CONTROL_PERIOD_US rather than
 * written out in microseconds, so retuning the loop rate moves the buckets with
 * it. The table holds ratios to the target period in permille.
 */
struct zb_hist {
	uint32_t bucket[ZB_HIST_BUCKETS];
	uint32_t count;
	uint32_t min_us;
	uint32_t max_us;
	uint64_t sum_us;
	/** Loop iterations the timer fired for while we were still busy. */
	uint32_t overruns;
};

/** Reset a histogram to empty. */
void zb_hist_reset(struct zb_hist *h);

/** Record one loop period, microseconds. */
void zb_hist_record(struct zb_hist *h, uint32_t period_us);

/** Record @p n missed loop deadlines. */
void zb_hist_record_overrun(struct zb_hist *h, uint32_t n);

/**
 * @brief Upper edge of the bucket containing the given percentile.
 *
 * @param permille percentile in permille: 500 for p50, 990 for p99,
 *                 999 for p99.9.
 *
 * Conservative by construction: it reports the top of the containing bucket,
 * never interpolates. Report p50, p99, p99.9 and max together. A max on its own
 * says nothing about whether the loop is healthy, and quoting it alone is how
 * you lose an audience that has seen this trick before.
 */
uint32_t zb_hist_percentile(const struct zb_hist *h, uint32_t permille);

/** Upper edge of bucket @p i in microseconds, or UINT32_MAX for the last. */
uint32_t zb_hist_bucket_edge_us(uint32_t i);

#ifdef __cplusplus
}
#endif

#endif /* ZENBEDDED_HIST_H_ */
