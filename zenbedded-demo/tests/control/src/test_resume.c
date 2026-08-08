/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/ztest.h>

#include "test_common.h"

/*
 * The most important tests in the repository.
 *
 * When ros2_control reactivates a hardware interface it will push whatever
 * command was sitting in its buffer. If the joint has spent the last ten
 * seconds sagging to its park position, that buffered command is now half a
 * radian away, and applying it at full gain slams the arm. Everything below
 * exists to prove that cannot happen:
 *
 *   - the MCU refuses a resume command that is not near where it actually is,
 *   - once it accepts one, it ramps the gains rather than snapping to them,
 *   - and across an entire FOLLOWING -> PARK -> resume cycle, neither the
 *     commanded effort nor the joint velocity ever steps discontinuously.
 *
 * A smooth resume is worth more to a robotics audience than the kill itself.
 */

#define ZB_RESUME_EPSILON_RAD ((float)CONFIG_ZENBEDDED_RESUME_EPSILON_MRAD * 1e-3f)

/*
 * Continuity budgets, per 1 ms control step.
 *
 * Scaled off the joint's own effort ceiling rather than written as bare
 * numbers, so retuning the joint in devicetree does not quietly loosen them.
 * A slam looks like effort going to the rail inside one or two steps; these
 * budgets are a small fraction of that.
 */
#define ZB_MAX_EFFORT_STEP_FRAC 0.05f
#define ZB_MAX_VELOCITY_STEP    0.05f /* rad/s per step = 50 rad/s^2 */

static struct zb_test_ctx t;

/** Raise to @p pos under a healthy host, then let it age all the way to parked. */
static void cycle_to_parked(float pos)
{
	zb_test_init(&t, pos);
	zb_test_cmd(&t, pos);
	zb_test_step(&t, 1);
	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(1000), pos, ZB_TEST_STEPS_MS(10));
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING);

	/* Host dies. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zb_test_step(&t, ZB_TEST_STEPS_MS(12000));

	zassert_equal(t.rt.mode, ZB_MODE_PARK);
	zassert_true(t.rt.parked, "joint should be fully parked before the resume tests");
}

ZTEST_SUITE(zb_resume, NULL, NULL, NULL, NULL, NULL);

ZTEST(zb_resume, test_stale_far_command_is_rejected)
{
	cycle_to_parked(1.0f);

	uint32_t rejected_before = t.rt.rejected_cmds;
	uint32_t accepted_before = t.rt.accepted_cmds;

	/* Exactly what ros2_control would flush out of a stale buffer. */
	zb_test_cmd(&t, 1.0f);
	zb_test_step(&t, 1);

	zassert_equal(t.rt.rejected_cmds, rejected_before + 1U,
		      "a stale command far from the current position must be rejected");
	zassert_equal(t.rt.accepted_cmds, accepted_before, "and must not be accepted");
	zassert_equal(t.rt.mode, ZB_MODE_PARK, "a rejected command does not resume the joint");
	zassert_false(t.rt.driver_enabled, "and does not re-energise the driver");
}

ZTEST(zb_resume, test_stale_command_rejected_repeatedly)
{
	cycle_to_parked(1.0f);

	/* A host that keeps retrying the stale value gets nowhere, forever. */
	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(1000); i++) {
		zb_test_cmd(&t, 1.0f);
		zb_test_step(&t, 1);
		zassert_equal(t.rt.mode, ZB_MODE_PARK, "resumed on a stale command at step %u", i);
	}

	zassert_false(t.rt.driver_enabled);
}

ZTEST(zb_resume, test_command_at_reported_position_resumes)
{
	cycle_to_parked(1.0f);

	/*
	 * What on_activate() is supposed to do: seed the command buffer from
	 * the MCU's *reported* position, never from a cached or default value.
	 */
	float reported = t.rt.position;

	zb_test_cmd(&t, reported);
	zb_test_step(&t, 1);

	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING, "a command at the reported position resumes");
	zassert_true(t.rt.driver_enabled);
	zassert_true(t.rt.ramping, "resume must start a gain ramp");
	zassert_within(t.rt.gain_scale, 0.0f, 0.05f, "gains must start near zero");
}

ZTEST(zb_resume, test_epsilon_is_the_boundary)
{
	cycle_to_parked(1.0f);

	float reported = t.rt.position;

	/* Just outside the window: refused. */
	zb_test_cmd(&t, reported + ZB_RESUME_EPSILON_RAD * 1.5f);
	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_PARK, "command outside the epsilon window was accepted");

	/* Just inside: accepted. */
	zb_test_cmd(&t, reported + ZB_RESUME_EPSILON_RAD * 0.5f);
	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING,
		      "command inside the epsilon window was refused");
}

ZTEST(zb_resume, test_gains_ramp_to_full)
{
	cycle_to_parked(1.0f);

	float reported = t.rt.position;

	zb_test_cmd(&t, reported);
	zb_test_step(&t, 1);

	float prev = t.rt.gain_scale;

	/* Monotonic, and complete no sooner than the configured ramp. */
	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_RESUME_RAMP_MS) - 2U; i++) {
		zb_test_cmd(&t, reported);
		zb_test_step(&t, 1);

		zassert_true(t.rt.gain_scale >= prev, "gain ramp went backwards at step %u", i);
		zassert_true(t.rt.gain_scale <= 1.0f, "gain scale exceeded unity");
		prev = t.rt.gain_scale;
	}

	zassert_true(t.rt.ramping, "ramp finished early");

	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(50), reported, ZB_TEST_STEPS_MS(10));

	zassert_false(t.rt.ramping, "ramp never finished");
	zassert_within(t.rt.gain_scale, 1.0f, 1e-6f, "gains must return to full");
}

ZTEST(zb_resume, test_full_cycle_has_no_discontinuity)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];
	float effort_budget = c->max_effort * ZB_MAX_EFFORT_STEP_FRAC;

	/*
	 * The whole story, end to end, with continuity measured across every
	 * step of it: healthy tracking, host death, degrade, hold, park, a
	 * stale command refused, then a correct resume.
	 */
	zb_test_init(&t, 1.0f);
	zb_test_cmd(&t, 1.0f);
	zb_test_step(&t, 1);
	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(1000), 1.0f, ZB_TEST_STEPS_MS(10));
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING);

	/* Host dies mid-motion. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zb_test_step(&t, ZB_TEST_STEPS_MS(12000));
	zassert_equal(t.rt.mode, ZB_MODE_PARK);
	zassert_true(t.rt.parked);

	/* Host returns and flushes its stale buffer. Refused. */
	zb_test_cmd(&t, 1.0f);
	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_PARK);

	/* Host re-reads the reported position and seeds from it. Accepted. */
	float reported = t.rt.position;

	zb_test_cmd(&t, reported);
	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING);

	/* Ride the ramp back to full authority. */
	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_RESUME_RAMP_MS + 500),
				reported, ZB_TEST_STEPS_MS(10));
	zassert_false(t.rt.ramping);

	TC_PRINT("full cycle: worst effort step %f Nm (budget %f), "
		 "worst velocity step %f rad/s (budget %f)\n",
		 (double)t.max_effort_jump, (double)effort_budget, (double)t.max_velocity_jump,
		 (double)ZB_MAX_VELOCITY_STEP);

	zassert_true(t.max_effort_jump <= effort_budget,
		     "effort stepped %f Nm in one 1 ms iteration, budget is %f Nm: "
		     "that is a slam",
		     (double)t.max_effort_jump, (double)effort_budget);
	zassert_true(t.max_velocity_jump <= ZB_MAX_VELOCITY_STEP,
		     "joint velocity stepped %f rad/s in one 1 ms iteration, budget is %f",
		     (double)t.max_velocity_jump, (double)ZB_MAX_VELOCITY_STEP);
}

ZTEST(zb_resume, test_resume_without_the_ramp_would_slam)
{
	/*
	 * A control test that only ever passes is not evidence of anything. This
	 * one establishes that the continuity budget in the test above is tight
	 * enough to catch the failure it is meant to catch: drive the same
	 * resume with the gain ramp already complete, and the joint's response
	 * to being handed authority all at once must blow the budget.
	 */
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];
	float effort_budget = c->max_effort * ZB_MAX_EFFORT_STEP_FRAC;

	cycle_to_parked(1.0f);

	float reported = t.rt.position;

	zb_test_cmd(&t, reported);
	zb_test_step(&t, 1);

	/* Skip the ramp, exactly as an implementation without one would. */
	t.rt.ramping = false;
	t.rt.gain_scale = 1.0f;
	zb_test_step(&t, 1);

	/* Start measuring from the settled state, just before the bad thing. */
	zb_test_continuity_reset(&t);

	/* Now hand it the far command the epsilon gate would have refused. */
	t.rt.active.position = 1.0f;
	zb_test_step(&t, 2);

	TC_PRINT("unguarded resume: effort step %f Nm against a %f Nm budget\n",
		 (double)t.max_effort_jump, (double)effort_budget);

	zassert_true(t.max_effort_jump > effort_budget,
		     "the continuity budget is too loose to detect a real slam");
}
