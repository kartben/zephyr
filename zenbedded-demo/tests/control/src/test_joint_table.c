/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>

#include "test_common.h"

/*
 * The joint table is the contract between devicetree and the control core. If
 * these break, either a binding changed or a unit conversion is wrong, and
 * every other test in this directory is testing the wrong joint.
 */

ZTEST_SUITE(zb_joint_table, NULL, NULL, NULL, NULL, NULL);

ZTEST(zb_joint_table, test_topology_from_devicetree)
{
	zassert_equal(ZB_JOINT_COUNT, 1, "Phase 1 populates exactly one joint");
	zassert_str_equal(ZB_ROBOT_ID, "arm0");
	zassert_str_equal(zb_joints[ZB_TEST_JOINT].name, "shoulder");
}

ZTEST(zb_joint_table, test_units_converted_from_milli)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	/* Devicetree carries milli-units; the table must hold float SI. */
	zassert_within(c->limit_lo, -1.570f, 1e-6f, "limit-lo-mrad -> rad");
	zassert_within(c->limit_hi, 1.570f, 1e-6f, "limit-hi-mrad -> rad");
	zassert_within(c->max_effort, 0.400f, 1e-6f, "max-effort-mnm -> Nm");
	zassert_within(c->hold_effort, 0.150f, 1e-6f, "hold-effort-mnm -> Nm");
	zassert_within(c->kp, 1.200f, 1e-6f);
	zassert_within(c->ki, 0.080f, 1e-6f);
	zassert_within(c->kd, 0.045f, 1e-6f);
}

ZTEST(zb_joint_table, test_defaults_applied)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	/* Not set in the overlay, so these come from the binding defaults. */
	zassert_within(c->park_target, 0.0f, 1e-6f);
	zassert_within(c->park_rate, 0.300f, 1e-6f);
	zassert_true(c->sim_inertia > 0.0f);
}

ZTEST(zb_joint_table, test_safety_invariants)
{
	const struct zb_joint_cfg *c = &zb_joints[ZB_TEST_JOINT];

	zassert_true(c->hold_effort <= c->max_effort, "hold effort must not exceed max effort");
	zassert_true(c->limit_lo < c->limit_hi);
	zassert_true(c->park_target >= c->limit_lo && c->park_target <= c->limit_hi);
}
