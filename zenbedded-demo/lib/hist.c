/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>

#include <zenbedded/hist.h>

/*
 * Bucket edges as a ratio to the target loop period, in permille. Keeping them
 * relative means retuning CONFIG_ZENBEDDED_CONTROL_PERIOD_US moves the buckets
 * with it, and the resolution stays where it is useful: packed tightly around
 * the target, coarse out in the tail where you only care about the order of
 * magnitude.
 */
static const uint32_t zb_hist_ratio_permille[ZB_HIST_BUCKETS] = {
	500,  800,  900,  950,  980,  1000, 1020,  1050,
	1100, 1200, 1500, 2000, 3000, 5000, 10000, UINT32_MAX,
};

#define ZB_PERIOD_US ((uint64_t)CONFIG_ZENBEDDED_CONTROL_PERIOD_US)

uint32_t zb_hist_bucket_edge_us(uint32_t i)
{
	if (i >= ZB_HIST_BUCKETS || zb_hist_ratio_permille[i] == UINT32_MAX) {
		return UINT32_MAX;
	}

	return (uint32_t)((ZB_PERIOD_US * zb_hist_ratio_permille[i]) / 1000U);
}

void zb_hist_reset(struct zb_hist *h)
{
	memset(h, 0, sizeof(*h));
	h->min_us = UINT32_MAX;
}

void zb_hist_record(struct zb_hist *h, uint32_t period_us)
{
	/* Bounded: at most ZB_HIST_BUCKETS comparisons, no matter the input. */
	for (uint32_t i = 0; i < ZB_HIST_BUCKETS; i++) {
		if (period_us < zb_hist_bucket_edge_us(i)) {
			h->bucket[i]++;
			break;
		}
	}

	h->count++;
	h->sum_us += period_us;

	if (period_us > h->max_us) {
		h->max_us = period_us;
	}
	if (period_us < h->min_us) {
		h->min_us = period_us;
	}
}

void zb_hist_record_overrun(struct zb_hist *h, uint32_t n)
{
	h->overruns += n;
}

uint32_t zb_hist_percentile(const struct zb_hist *h, uint32_t permille)
{
	if (h->count == 0U) {
		return 0U;
	}

	/* Rank of the sample we are looking for, rounded up. */
	uint64_t want = ((uint64_t)h->count * permille + 999U) / 1000U;

	if (want == 0U) {
		want = 1U;
	}

	uint64_t cum = 0;

	for (uint32_t i = 0; i < ZB_HIST_BUCKETS; i++) {
		cum += h->bucket[i];
		if (cum >= want) {
			uint32_t edge = zb_hist_bucket_edge_us(i);

			/*
			 * The open-ended top bucket has no upper edge to quote,
			 * so fall back to the largest period actually seen.
			 */
			return (edge == UINT32_MAX) ? h->max_us : edge;
		}
	}

	return h->max_us;
}
