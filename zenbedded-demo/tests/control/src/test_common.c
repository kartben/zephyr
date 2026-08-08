/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>

#include <zephyr/kernel.h>

#include "test_common.h"

void zb_test_init(struct zb_test_ctx *t, float start_pos)
{
	zb_plant_init();
	zb_plant_sim_set_state(ZB_TEST_JOINT, start_pos, 0.0f);
	zb_joint_rt_init(&t->rt, ZB_TEST_JOINT);

	t->now_us = 0;
	t->seq = 0;
	t->prev_effort = 0.0f;
	t->prev_velocity = 0.0f;
	t->have_prev = false;

	zb_test_continuity_reset(t);
}

void zb_test_continuity_reset(struct zb_test_ctx *t)
{
	/*
	 * Zero the maxima but keep the previous sample, so that the very first
	 * step after this call is still measured against the state the joint
	 * was actually in. Clearing prev_* here would silently swallow exactly
	 * the jump a caller is trying to measure.
	 */
	t->max_effort_jump = 0.0f;
	t->max_velocity_jump = 0.0f;
}

void zb_test_step(struct zb_test_ctx *t, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++) {
		t->now_us += ZB_TEST_PERIOD_US;
		zb_control_step(&t->rt, ZB_TEST_JOINT, t->now_us, ZB_TEST_DT);

		if (t->have_prev) {
			float de = fabsf(t->rt.effort - t->prev_effort);
			float dv = fabsf(t->rt.velocity - t->prev_velocity);

			if (de > t->max_effort_jump) {
				t->max_effort_jump = de;
			}
			if (dv > t->max_velocity_jump) {
				t->max_velocity_jump = dv;
			}
		}

		t->prev_effort = t->rt.effort;
		t->prev_velocity = t->rt.velocity;
		t->have_prev = true;
	}
}

void zb_test_cmd(struct zb_test_ctx *t, float position)
{
	struct zb_setpoint sp = {
		.seq = ++t->seq,
		.position = position,
		.velocity = 0.0f,
		.arrival_cycles = k_cycle_get_64(),
	};

	zb_sp_publish(&t->rt.sp, &sp);
}

void zb_test_step_commanding(struct zb_test_ctx *t, uint32_t n, float position,
			     uint32_t every_steps)
{
	for (uint32_t i = 0; i < n; i++) {
		if ((i % every_steps) == 0U) {
			zb_test_cmd(t, position);
		}
		zb_test_step(t, 1);
	}
}
