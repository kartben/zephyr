/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZENBEDDED_JOINT_H_
#define ZENBEDDED_JOINT_H_

#include <zephyr/devicetree.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile-time joint configuration.
 *
 * One instance per devicetree joint node with status "okay", built at compile
 * time into a const table in ROM. Devicetree carries integers in milli- and
 * micro-units; the conversion to float SI happens here, once, so that no
 * runtime code has to think about units again.
 */
struct zb_joint_cfg {
	/** Joint name, also a Zenoh key segment. */
	const char *name;

	/** Travel limits, radians. */
	float limit_lo;
	float limit_hi;

	/** Effort ceiling in FOLLOWING and DEGRADED, newton-metres. */
	float max_effort;
	/** Effort ceiling in HOLDING and PARK, newton-metres. */
	float hold_effort;

	/** PID gains, N*m/rad, N*m/(rad*s), N*m*s/rad. */
	float kp;
	float ki;
	float kd;

	/** Park descent target (radians) and rate limit (rad/s). */
	float park_target;
	float park_rate;

	/** Simulated plant parameters; unused by the hardware backend. */
	float sim_inertia; /**< kg*m^2 */
	float sim_damping; /**< N*m*s/rad */
	float sim_gravity; /**< N*m, peak gravity torque */
};

/** Devicetree node of the robot root. */
#define ZB_ROBOT_NODE DT_COMPAT_GET_ANY_STATUS_OKAY(zenbedded_robot)

BUILD_ASSERT(DT_NODE_EXISTS(ZB_ROBOT_NODE),
	     "no enabled \"zenbedded,robot\" node found in devicetree");

/** Robot identifier, first segment of every Zenoh key expression. */
#define ZB_ROBOT_ID DT_PROP(ZB_ROBOT_NODE, robot_id)

/** Number of enabled joints. */
#define ZB_JOINT_COUNT DT_CHILD_NUM_STATUS_OKAY(ZB_ROBOT_NODE)

BUILD_ASSERT(ZB_JOINT_COUNT > 0, "\"zenbedded,robot\" has no enabled joint children");

/**
 * @brief The joint table, in ROM.
 *
 * Indexed 0..ZB_JOINT_COUNT-1 in devicetree child order.
 */
extern const struct zb_joint_cfg zb_joints[ZB_JOINT_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* ZENBEDDED_JOINT_H_ */
