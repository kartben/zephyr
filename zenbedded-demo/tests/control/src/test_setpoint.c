/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include <zenbedded/setpoint.h>

#include "test_common.h"

/*
 * The setpoint channel is the only thing crossing the thread boundary, so it is
 * the only place a torn read can happen. Every setpoint carries redundant,
 * derivable fields, and the tests check them against the sequence number: if a
 * consumer ever sees this frame's position beside the previous frame's velocity,
 * the invariant fails and the channel is broken.
 */

#define ZB_POS_FOR_SEQ(s) ((float)(s) * 1e-3f)
#define ZB_VEL_FOR_SEQ(s) (-2.0f * ZB_POS_FOR_SEQ(s))
#define ZB_CYC_FOR_SEQ(s) ((uint64_t)(s) * 7ULL)

static struct zb_sp_chan chan;

static void fill(struct zb_setpoint *sp, uint32_t seq)
{
	sp->seq = seq;
	sp->position = ZB_POS_FOR_SEQ(seq);
	sp->velocity = ZB_VEL_FOR_SEQ(seq);
	sp->arrival_cycles = ZB_CYC_FOR_SEQ(seq);
}

static void assert_consistent(const struct zb_setpoint *sp)
{
	zassert_within(sp->position, ZB_POS_FOR_SEQ(sp->seq), 1e-9f,
		       "torn read: position does not match seq %u", sp->seq);
	zassert_within(sp->velocity, ZB_VEL_FOR_SEQ(sp->seq), 1e-9f,
		       "torn read: velocity does not match seq %u", sp->seq);
	zassert_equal(sp->arrival_cycles, ZB_CYC_FOR_SEQ(sp->seq),
		      "torn read: arrival_cycles does not match seq %u", sp->seq);
}

/** The three slot indices must always be a permutation of {0,1,2}. */
static void assert_permutation(const struct zb_sp_chan *c)
{
	uint32_t s = (uint32_t)(atomic_get((atomic_t *)&c->shared) & ZB_SP_IDX_MASK);
	uint32_t p = c->producer_idx;
	uint32_t r = c->consumer_idx;

	zassert_true(p < 3U && r < 3U && s < 3U, "slot index out of range");
	zassert_true(p != r && p != s && r != s,
		     "slot indices collided (%u,%u,%u): a writer and a reader can now "
		     "be in the same slot",
		     p, r, s);
}

static void sp_before(void *f)
{
	ARG_UNUSED(f);
	zb_sp_init(&chan);
}

ZTEST_SUITE(zb_setpoint, NULL, NULL, sp_before, NULL, NULL);

ZTEST(zb_setpoint, test_empty_channel_yields_nothing)
{
	struct zb_setpoint out;

	zassert_false(zb_sp_consume(&chan, &out), "a fresh channel has nothing to consume");
	assert_permutation(&chan);
}

ZTEST(zb_setpoint, test_publish_then_consume)
{
	struct zb_setpoint in, out;

	fill(&in, 42U);
	zb_sp_publish(&chan, &in);
	assert_permutation(&chan);

	zassert_true(zb_sp_consume(&chan, &out));
	assert_consistent(&out);
	zassert_equal(out.seq, 42U);

	zassert_false(zb_sp_consume(&chan, &out), "the same setpoint must not be returned twice");
}

ZTEST(zb_setpoint, test_newest_wins)
{
	struct zb_setpoint in, out;

	/*
	 * The control loop wants the newest command, not a backlog. Publishing
	 * over an unconsumed setpoint is correct behaviour, not data loss.
	 */
	for (uint32_t s = 1; s <= 10U; s++) {
		fill(&in, s);
		zb_sp_publish(&chan, &in);
		assert_permutation(&chan);
	}

	zassert_true(zb_sp_consume(&chan, &out));
	assert_consistent(&out);
	zassert_equal(out.seq, 10U, "consumer must see the newest published setpoint");
}

ZTEST(zb_setpoint, test_fuzzed_producer_interleaving)
{
	struct zb_setpoint in, out;
	uint32_t seq = 0;
	uint32_t last_seen = 0;
	uint32_t rng = 0x1234567U;

	/*
	 * Ragged interleaving: bursts of publishes against bursts of consumes,
	 * driven by a fixed LCG so a failure is reproducible.
	 */
	for (int round = 0; round < 20000; round++) {
		rng = rng * 1103515245U + 12345U;

		uint32_t pubs = (rng >> 16) & 0x3U;
		uint32_t cons = (rng >> 20) & 0x3U;

		for (uint32_t i = 0; i < pubs; i++) {
			fill(&in, ++seq);
			zb_sp_publish(&chan, &in);
			assert_permutation(&chan);
		}

		for (uint32_t i = 0; i < cons; i++) {
			if (zb_sp_consume(&chan, &out)) {
				assert_consistent(&out);
				zassert_true(out.seq > last_seen,
					     "sequence went backwards: %u after %u", out.seq,
					     last_seen);
				last_seen = out.seq;
			}
			assert_permutation(&chan);
		}
	}

	zassert_true(last_seen > 0U, "the fuzz loop never delivered anything");
}

/* -- Preemptive version: a producer that cuts in under the consumer. ------- */

#define ZB_FUZZ_ITERATIONS 20000

static uint32_t fuzz_published;

/*
 * Publishing from a timer callback puts the producer in interrupt context,
 * where it preempts the consumer at genuinely arbitrary points -- including
 * between the atomic_get and the atomic_set inside zb_sp_consume(), which is
 * the one interleaving the whole design has to survive.
 *
 * This is the strongest interleaving available under native_sim: a second
 * thread would not do it, because native_sim's clock only advances when the CPU
 * is idle, so two spinning threads never actually interleave. The consumer
 * below burns a microsecond per iteration precisely so that simulated time
 * advances and the timer can fire underneath it.
 */
static void fuzz_timer_fn(struct k_timer *timer)
{
	struct zb_setpoint in;

	ARG_UNUSED(timer);

	fill(&in, ++fuzz_published);
	zb_sp_publish(&chan, &in);
}

static K_TIMER_DEFINE(fuzz_timer, fuzz_timer_fn, NULL);

ZTEST(zb_setpoint, test_fuzzed_producer_preempts_consumer)
{
	struct zb_setpoint out;
	uint32_t last_seen = 0;
	uint32_t consumed = 0;

	fuzz_published = 0;
	k_timer_start(&fuzz_timer, K_USEC(10), K_USEC(10));

	for (int i = 0; i < ZB_FUZZ_ITERATIONS; i++) {
		if (zb_sp_consume(&chan, &out)) {
			assert_consistent(&out);
			zassert_true(out.seq > last_seen, "sequence went backwards: %u after %u",
				     out.seq, last_seen);
			last_seen = out.seq;
			consumed++;
		}

		k_busy_wait(1);
	}

	k_timer_stop(&fuzz_timer);

	zassert_true(fuzz_published > 0U, "the producer never ran; the test proved nothing");
	zassert_true(consumed > 0U, "the consumer never saw anything");

	TC_PRINT("preemptive fuzz: %u published, %u consumed\n", fuzz_published, consumed);
}
