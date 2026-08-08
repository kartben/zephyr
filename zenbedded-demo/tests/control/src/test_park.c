/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/ztest.h>

#include "test_common.h"

/*
 * PARK is where a joint with no host ends up, so it is the mode that decides
 * whether "the host died" means "the arm settled" or "the arm dropped".
 */

static struct zb_test_ctx t;

/** Raise the joint to @p pos under a healthy host, then go silent. */
static void raise_then_abandon(float pos)
{
	zb_test_init(&t, pos);
	zb_test_cmd(&t, pos);
	zb_test_step(&t, 1);
	zassert_equal(t.rt.mode, ZB_MODE_FOLLOWING);

	zb_test_step_commanding(&t, ZB_TEST_STEPS_MS(1000), pos, ZB_TEST_STEPS_MS(10));
}

ZTEST_SUITE(zb_park, NULL, NULL, NULL, NULL, NULL);

ZTEST(zb_park, test_descent_is_rate_limited)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	raise_then_abandon(1.0f);

	/* Age out into PARK. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zassert_equal(t.rt.mode, ZB_MODE_PARK);

	float max_step = c->park_rate * ZB_TEST_DT;
	float worst = 0.0f;
	float prev = t.rt.target;

	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(8000); i++) {
		zb_test_step(&t, 1);

		float delta = fabsf(t.rt.target - prev);

		if (delta > worst) {
			worst = delta;
		}
		prev = t.rt.target;

		zassert_true(delta <= max_step * 1.001f + 1e-9f,
			     "PARK target moved %f rad in one step, limit is %f rad", (double)delta,
			     (double)max_step);
	}

	TC_PRINT("park: worst target step %f rad (limit %f)\n", (double)worst, (double)max_step);
}

ZTEST(zb_park, test_descent_takes_at_least_the_rate_limited_time)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	raise_then_abandon(1.0f);

	float start = t.rt.position;
	float travel = fabsf(start - c->park_target);
	uint32_t min_ms = (uint32_t)((travel / c->park_rate) * 1000.0f);

	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zassert_equal(t.rt.mode, ZB_MODE_PARK);

	/* Halfway through the rate-limited descent it cannot be finished. */
	zb_test_step(&t, ZB_TEST_STEPS_MS(min_ms / 2U));
	zassert_true(fabsf(t.rt.position - c->park_target) > 0.1f,
		     "joint reached park in half the rate-limited time: it fell, it did not "
		     "descend");
	zassert_true(t.rt.driver_enabled, "driver must stay engaged during the descent");
}

ZTEST(zb_park, test_settles_then_disables_the_driver)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	raise_then_abandon(1.0f);

	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zb_test_step(&t, ZB_TEST_STEPS_MS(10000));

	zassert_equal(t.rt.mode, ZB_MODE_PARK);
	zassert_true(t.rt.parked, "descent never completed");
	zassert_false(t.rt.driver_enabled, "PARK must end with the driver disabled, coasting");
	zassert_within(t.rt.effort, 0.0f, 1e-9f, "a disabled driver commands no effort");
	zassert_within(t.rt.position, c->park_target, 0.05f, "joint did not come to rest at park");
}

ZTEST(zb_park, test_stays_disabled_once_parked)
{
	raise_then_abandon(1.0f);

	zb_test_step(&t, ZB_TEST_STEPS_MS(CONFIG_ZENBEDDED_AGE_PARK_MS));
	zb_test_step(&t, ZB_TEST_STEPS_MS(10000));
	zassert_true(t.rt.parked);

	/*
	 * The parked state is latched. A joint that drifts a hair after the
	 * driver is cut must not re-energise, chatter, and re-park forever.
	 */
	for (uint32_t i = 0; i < ZB_TEST_STEPS_MS(5000); i++) {
		zb_test_step(&t, 1);
		zassert_false(t.rt.driver_enabled, "driver re-energised after parking, at step %u",
			      i);
	}
}
