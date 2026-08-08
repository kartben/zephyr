/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zenbedded/joint.h>

/*
 * The joint table is built here and nowhere else. Every limit, gain and rate
 * comes from devicetree; adding a joint is a DT edit and a rebuild.
 *
 * Devicetree is integral, so the properties arrive in milli- and micro-units.
 * They are converted to float SI exactly once, at compile time, which is why
 * nothing downstream has to carry a unit suffix around.
 */
#define ZB_MILLI(x) ((float)(x) * 1e-3f)
#define ZB_MICRO(x) ((float)(x) * 1e-6f)

#define ZB_JOINT_ENTRY(node)                                                                       \
	{                                                                                          \
		.name = DT_PROP(node, joint_name),                                                 \
		.limit_lo = ZB_MILLI(DT_PROP(node, limit_lo_mrad)),                                \
		.limit_hi = ZB_MILLI(DT_PROP(node, limit_hi_mrad)),                                \
		.max_effort = ZB_MILLI(DT_PROP(node, max_effort_mnm)),                             \
		.hold_effort = ZB_MILLI(DT_PROP(node, hold_effort_mnm)),                           \
		.kp = ZB_MILLI(DT_PROP(node, pid_kp_milli)),                                       \
		.ki = ZB_MILLI(DT_PROP(node, pid_ki_milli)),                                       \
		.kd = ZB_MILLI(DT_PROP(node, pid_kd_milli)),                                       \
		.park_target = ZB_MILLI(DT_PROP(node, park_target_mrad)),                          \
		.park_rate = ZB_MILLI(DT_PROP(node, park_rate_mrad_s)),                            \
		.sim_inertia = ZB_MICRO(DT_PROP(node, sim_inertia_ukgm2)),                         \
		.sim_damping = ZB_MICRO(DT_PROP(node, sim_damping_unms)),                          \
		.sim_gravity = ZB_MILLI(DT_PROP(node, sim_gravity_mnm)),                           \
	}

const struct zb_joint_cfg zb_joints[ZB_JOINT_COUNT] = {
	DT_FOREACH_CHILD_STATUS_OKAY_SEP(ZB_ROBOT_NODE, ZB_JOINT_ENTRY, (, ))};

/*
 * Catch the configuration mistakes that are survivable in a build and very much
 * not survivable in a gravity-loaded joint.
 */
#define ZB_JOINT_CHECK(node)                                                                       \
	BUILD_ASSERT(DT_PROP(node, limit_lo_mrad) < DT_PROP(node, limit_hi_mrad),                  \
		     "joint limit-lo-mrad must be below limit-hi-mrad");                           \
	BUILD_ASSERT(DT_PROP(node, hold_effort_mnm) <= DT_PROP(node, max_effort_mnm),              \
		     "joint hold-effort-mnm must not exceed max-effort-mnm");                      \
	BUILD_ASSERT(DT_PROP(node, max_effort_mnm) > 0, "joint max-effort-mnm must be positive");  \
	BUILD_ASSERT(DT_PROP(node, park_rate_mrad_s) > 0,                                          \
		     "joint park-rate-mrad-s must be positive, or PARK never descends");           \
	BUILD_ASSERT(DT_PROP(node, park_target_mrad) >= DT_PROP(node, limit_lo_mrad) &&            \
			     DT_PROP(node, park_target_mrad) <= DT_PROP(node, limit_hi_mrad),      \
		     "joint park-target-mrad must lie within the travel limits");                  \
	BUILD_ASSERT(DT_PROP(node, sim_inertia_ukgm2) > 0,                                         \
		     "joint sim-inertia-ukgm2 must be positive");

DT_FOREACH_CHILD_STATUS_OKAY(ZB_ROBOT_NODE, ZB_JOINT_CHECK)
