/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZB_TEST_COMMON_H_
#define ZB_TEST_COMMON_H_

#include <stdint.h>

#include <zenbedded/control.h>
#include <zenbedded/joint.h>
#include <zenbedded/plant.h>

/** The joint under test. Phase 1 populates exactly one. */
#define ZB_TEST_JOINT 0

#define ZB_TEST_PERIOD_US ((uint64_t)CONFIG_ZENBEDDED_CONTROL_PERIOD_US)
#define ZB_TEST_DT        ((float)CONFIG_ZENBEDDED_CONTROL_PERIOD_US * 1e-6f)

/** Control steps in @p ms milliseconds. */
#define ZB_TEST_STEPS_MS(ms) ((uint32_t)(((uint64_t)(ms) * 1000ULL) / ZB_TEST_PERIOD_US))

/**
 * @brief Harness that drives the control core in virtual time.
 *
 * The control step is a plain function of state and inputs, so the tests call
 * it directly rather than starting the 1 kHz thread and racing it. Every result
 * here is bit-identical run to run, which is what makes a threshold test worth
 * asserting on.
 */
struct zb_test_ctx {
	struct zb_joint_rt rt;
	uint64_t now_us;
	uint32_t seq;

	/* Continuity tracking, updated on every step. */
	float prev_effort;
	float prev_velocity;
	float max_effort_jump;
	float max_velocity_jump;
	bool have_prev;
};

/** Reset plant and controller, placing the joint at @p start_pos at rest. */
void zb_test_init(struct zb_test_ctx *t, float start_pos);

/** Run @p n control steps, advancing virtual time. */
void zb_test_step(struct zb_test_ctx *t, uint32_t n);

/** Publish a position command, as the comms thread would. */
void zb_test_cmd(struct zb_test_ctx *t, float position);

/**
 * @brief Zero the continuity maxima so a later window can be measured alone.
 *
 * The previous sample is kept, so the first step after this call is still
 * compared against the state the joint was in when it was called.
 */
void zb_test_continuity_reset(struct zb_test_ctx *t);

/**
 * @brief Run @p n steps while commanding @p position every @p every_ms.
 *
 * Stands in for a healthy host publishing at ~100 Hz.
 */
void zb_test_step_commanding(struct zb_test_ctx *t, uint32_t n, float position,
			     uint32_t every_steps);

#endif /* ZB_TEST_COMMON_H_ */
