/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/ztest.h>

#include "test_common.h"

/*
 * The failover policy, asserted at its exact thresholds. These numbers are the
 * demo: if they drift, the thing on the slide is no longer the thing in the
 * firmware.
 */

#define ZB_DEGRADED_MS CONFIG_ZENBEDDED_AGE_DEGRADED_MS
#define ZB_HOLDING_MS  CONFIG_ZENBEDDED_AGE_HOLDING_MS
#define ZB_PARK_MS     CONFIG_ZENBEDDED_AGE_PARK_MS

static struct zb_test_ctx t;

/** Bring the joint from cold start into FOLLOWING at @p pos. */
static void enter_following(float pos)
{
	zb_test_init(&t, pos);

	zassert_equal(t.rt.mode, ZB_MODE_PARK, "a joint that has never heard a host is parked");

	zb_test_cmd(&t, pos);
	zb_test_step(&t, 1);

	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING, "a command at the current position resumes");
}

ZTEST_SUITE(zb_state_machine, NULL, NULL, NULL, NULL, NULL);

ZTEST(zb_state_machine, test_cold_start_is_parked_and_unpowered)
{
	zb_test_init(&t, 0.0f);
	zb_test_step(&t, 10);

	zassert_equal(t.rt.mode, ZB_MODE_PARK);
	zassert_false(t.rt.driver_enabled, "never energise a motor on no information");
	zassert_within(t.rt.effort, 0.0f, 1e-9f);
	zassert_false(t.rt.ever_accepted);
}

ZTEST(zb_state_machine, test_transitions_fire_at_thresholds)
{
	enter_following(0.0f);

	/* One step short of the DEGRADED threshold. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_DEGRADED_MS) - 1U);
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING, "still FOLLOWING at %d ms - 1 step",
		      ZB_DEGRADED_MS);

	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_DEGRADED, "DEGRADED exactly at %d ms", ZB_DEGRADED_MS);

	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_HOLDING_MS - ZB_DEGRADED_MS) - 1U);
	zassert_equal(t.rt.mode, ZB_MODE_DEGRADED, "still DEGRADED at %d ms - 1 step",
		      ZB_HOLDING_MS);

	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_HOLDING, "HOLDING exactly at %d ms", ZB_HOLDING_MS);

	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_PARK_MS - ZB_HOLDING_MS) - 1U);
	zassert_equal(t.rt.mode, ZB_MODE_HOLDING, "still HOLDING at %d ms - 1 step", ZB_PARK_MS);

	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_PARK, "PARK exactly at %d ms", ZB_PARK_MS);
}

ZTEST(zb_state_machine, test_fresh_commands_keep_it_following)
{
	enter_following(0.0f);

	/* A healthy host at ~100 Hz: the joint must never leave FOLLOWING. */
	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(2000); i++) {
		if ((i % ZB_TEST_STEPS_MS(10)) == 0U) {
			zb_test_cmd(&t, 0.0f);
		}
		zb_test_step(&t, 1);
		zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING, "dropped out of FOLLOWING at step %u",
			      i);
	}
}

ZTEST(zb_state_machine, test_age_keys_off_arrival_not_link_state)
{
	enter_following(0.0f);

	/*
	 * Nothing here ever tells the state machine the link is down. A host
	 * that holds its socket open and publishes nothing looks exactly like a
	 * pulled cable, which is the entire point.
	 */
	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_PARK_MS));
	zassert_equal(t.rt.mode, ZB_MODE_PARK);
}

ZTEST(zb_state_machine, test_effort_ceiling_by_mode)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	/* Out at an angle, where holding station actually costs torque. */
	enter_following(1.0f);
	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(1000), 1.0f, ZB_TEST_STEPS_MS(10));

	zassert_true(fabsf(t.rt.effort) <= c->max_effort + 1e-6f, "FOLLOWING exceeded max effort");

	/* Go silent and walk through the modes, checking the ceiling in force. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_DEGRADED_MS));
	zassert_equal(t.rt.mode, ZB_MODE_DEGRADED);
	zassert_true(fabsf(t.rt.effort) <= c->max_effort + 1e-6f, "DEGRADED exceeded max effort");

	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_HOLDING_MS - ZB_DEGRADED_MS));
	zassert_equal(t.rt.mode, ZB_MODE_HOLDING);

	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(ZB_PARK_MS - ZB_HOLDING_MS) - 1U; i++) {
		zb_test_step(&t, 1);
		zassert_true(fabsf(t.rt.effort) <= c->hold_effort + 1e-6f,
			     "HOLDING drew %f Nm, above the %f Nm clamp",
			     (double)fabsf(t.rt.effort), (double)c->hold_effort);
	}
}

ZTEST(zb_state_machine, test_holding_actually_holds)
{
	/*
	 * The HOLDING clamp is only a sane policy if it leaves enough torque to
	 * resist gravity. If this fails, the joint is sagging through HOLDING
	 * and the PARK descent that follows is a fall, not a descent.
	 */
	enter_following(1.0f);
	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(1000), 1.0f, ZB_TEST_STEPS_MS(10));

	/* One final command, so that silence starts from a known age of zero. */
	zb_test_cmd(&t, 1.0f);
	zb_test_step(&t, 1);

	float held = t.rt.position;

	zb_test_step(&t, ZB_TEST_STEPS_MS(ZB_PARK_MS) - 1U);
	zassert_equal(t.rt.mode, ZB_MODE_HOLDING);

	zassert_within(t.rt.position, held, 0.05f,
		       "joint drifted %f rad while HOLDING; hold-effort-mnm is too low for "
		       "sim-gravity-mnm",
		       (double)fabsf(t.rt.position - held));
}
