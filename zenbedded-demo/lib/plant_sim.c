/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>

#include <zephyr/sys/util.h>

#include <zenbedded/joint.h>
#include <zenbedded/plant.h>

/*
 * Second-order gravity-loaded plant:
 *
 *     J*theta'' = tau - b*theta' - G*sin(theta)
 *
 * with J, b and G taken from devicetree. Integrated semi-implicitly (velocity
 * first, then position from the new velocity), which stays stable at 1 kHz for
 * the stiffnesses this demo uses and does not accumulate energy the way plain
 * explicit Euler does.
 *
 * G is the peak gravity torque, so the gravitational rest position is theta = 0
 * and the torque needed to hold station rises with sin(theta). That is what
 * makes the failover interesting: a joint held out at an angle falls if nobody
 * is driving it.
 */

struct zb_sim_joint {
	float position;
	float velocity;
	float effort;
	bool enabled;
};

static struct zb_sim_joint zb_sim[ZB_JOINT_COUNT];

void zb_plant_init(void)
{
	for (uint8_t i = 0; i < ZB_JOINT_COUNT; i++) {
		memset(&zb_sim[i], 0, sizeof(zb_sim[i]));
		zb_sim[i].position = zb_joints[i].park_target;
	}
}

void zb_plant_enable(uint8_t idx, bool enable)
{
	zb_sim[idx].enabled = enable;
}

void zb_plant_apply_effort(uint8_t idx, float effort)
{
	zb_sim[idx].effort = effort;
}

void zb_plant_read(uint8_t idx, float *position, float *velocity)
{
	*position = zb_sim[idx].position;
	*velocity = zb_sim[idx].velocity;
}

void zb_plant_update(uint8_t idx, float dt)
{
	const struct zb_joint_cfg *cfg = &zb_joints[idx];
	struct zb_sim_joint *s = &zb_sim[idx];

	/* A disabled driver applies no torque. The joint coasts under gravity. */
	float tau = s->enabled ? s->effort : 0.0f;

	float accel =
		(tau - cfg->sim_damping * s->velocity - cfg->sim_gravity * sinf(s->position)) /
		cfg->sim_inertia;

	s->velocity += accel * dt;
	s->position += s->velocity * dt;

	/* Hard stops. A real joint has them; the simulated one should too. */
	if (s->position < cfg->limit_lo) {
		s->position = cfg->limit_lo;
		s->velocity = MAX(s->velocity, 0.0f);
	} else if (s->position > cfg->limit_hi) {
		s->position = cfg->limit_hi;
		s->velocity = MIN(s->velocity, 0.0f);
	}
}

void zb_plant_sim_set_state(uint8_t idx, float position, float velocity)
{
	zb_sim[idx].position = position;
	zb_sim[idx].velocity = velocity;
}
