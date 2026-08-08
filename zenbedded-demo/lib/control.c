/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <string.h>

#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#include <zenbedded/control.h>
#include <zenbedded/plant.h>

/*
 * Everything in this file runs on the control thread. There is no logging, no
 * allocation, no blocking call and no unbounded loop anywhere below this line,
 * and there must not be. If you are about to add one, you are about to break
 * the only claim the demo makes.
 */

#define ZB_AGE_DEGRADED_US ((uint64_t)CONFIG_ZENBEDDED_AGE_DEGRADED_MS * 1000ULL)
#define ZB_AGE_HOLDING_US  ((uint64_t)CONFIG_ZENBEDDED_AGE_HOLDING_MS * 1000ULL)
#define ZB_AGE_PARK_US     ((uint64_t)CONFIG_ZENBEDDED_AGE_PARK_MS * 1000ULL)

#define ZB_RESUME_EPSILON_RAD ((float)CONFIG_ZENBEDDED_RESUME_EPSILON_MRAD * 1e-3f)
#define ZB_RESUME_RAMP_US     ((uint64_t)CONFIG_ZENBEDDED_RESUME_RAMP_MS * 1000ULL)

#define ZB_PARK_SETTLE_RAD   ((float)CONFIG_ZENBEDDED_PARK_SETTLE_MRAD * 1e-3f)
#define ZB_PARK_SETTLE_RAD_S ((float)CONFIG_ZENBEDDED_PARK_SETTLE_MRAD_S * 1e-3f)

BUILD_ASSERT(ZB_AGE_DEGRADED_US < ZB_AGE_HOLDING_US && ZB_AGE_HOLDING_US < ZB_AGE_PARK_US,
	     "failover thresholds must be strictly increasing");

const char *zb_mode_name(enum zb_mode mode)
{
	switch (mode) {
	case ZB_MODE_FOLLOWING:
		return "FOLLOWING";
	case ZB_MODE_DEGRADED:
		return "DEGRADED";
	case ZB_MODE_HOLDING:
		return "HOLDING";
	case ZB_MODE_PARK:
		return "PARK";
	default:
		return "?";
	}
}

void zb_joint_rt_init(struct zb_joint_rt *rt, uint8_t idx)
{
	const struct zb_joint_cfg *cfg = &zb_joints[idx];

	memset(rt, 0, sizeof(*rt));

	rt->cfg = cfg;
	zb_sp_init(&rt->sp);

	/*
	 * A joint that has never heard from a host starts parked with the
	 * driver disabled. Any other initial state is a decision to energise a
	 * motor on the strength of no information at all.
	 */
	rt->mode = ZB_MODE_PARK;
	rt->target = cfg->park_target;
	rt->gain_scale = 1.0f;
	rt->parked = true;
	rt->driver_enabled = false;
	rt->ever_accepted = false;
}

/** Map command age to failover mode. The whole policy, in four lines. */
static enum zb_mode zb_mode_for_age(uint64_t age_us)
{
	if (age_us < ZB_AGE_DEGRADED_US) {
		return ZB_MODE_FOLLOWING;
	}
	if (age_us < ZB_AGE_HOLDING_US) {
		return ZB_MODE_DEGRADED;
	}
	if (age_us < ZB_AGE_PARK_US) {
		return ZB_MODE_HOLDING;
	}

	return ZB_MODE_PARK;
}

/**
 * Take the newest command, if the comms thread published one.
 *
 * The resume gate lives here. Once the joint has left FOLLOWING it will not go
 * back until it is handed a command close to where it actually is. That is what
 * stops ros2_control from flushing a stale buffered command into a joint that
 * has since sagged half a radian, which is the failure everybody discovers at
 * the worst possible moment.
 */
static void zb_take_command(struct zb_joint_rt *rt, uint64_t now_us)
{
	const struct zb_joint_cfg *cfg = rt->cfg;
	struct zb_setpoint sp;

	if (!zb_sp_consume(&rt->sp, &sp)) {
		return;
	}

	float want = CLAMP(sp.position, cfg->limit_lo, cfg->limit_hi);

	/*
	 * Gate every re-entry into FOLLOWING, including the one from PARK and
	 * the very first command after boot. The spec calls out DEGRADED and
	 * HOLDING; PARK and cold start are the same hazard with more distance
	 * accumulated, so they get the same treatment.
	 */
	bool gated = !rt->ever_accepted || (rt->mode != ZB_MODE_FOLLOWING);

	if (gated && fabsf(want - rt->position) > ZB_RESUME_EPSILON_RAD) {
		rt->rejected_cmds++;
		return;
	}

	rt->active = sp;
	rt->active.position = want;
	rt->last_accept_us = now_us;
	rt->accepted_cmds++;

	if (gated) {
		/*
		 * Resuming. Start the gains at zero and ramp them, so the joint
		 * eases back under control instead of snapping to it. The
		 * integrator goes too: whatever it wound up while holding is
		 * about to be wrong.
		 */
		rt->ramping = true;
		rt->resume_start_us = now_us;
		rt->gain_scale = 0.0f;
		rt->integrator = 0.0f;
		rt->target = rt->position;
	}

	rt->ever_accepted = true;
}

/** Advance the commanded target for the current mode. */
static void zb_update_target(struct zb_joint_rt *rt, enum zb_mode mode, float dt)
{
	const struct zb_joint_cfg *cfg = rt->cfg;

	switch (mode) {
	case ZB_MODE_FOLLOWING:
		rt->target = rt->active.position;
		break;

	case ZB_MODE_DEGRADED:
	case ZB_MODE_HOLDING:
		/* Hold the last valid setpoint. */
		rt->target = rt->active.position;
		break;

	case ZB_MODE_PARK:
	default: {
		/*
		 * Rate-limited descent. This is the difference between a joint
		 * settling to rest and a joint dropping.
		 */
		float step = cfg->park_rate * dt;
		float delta = cfg->park_target - rt->target;

		if (delta > step) {
			rt->target += step;
		} else if (delta < -step) {
			rt->target -= step;
		} else {
			rt->target = cfg->park_target;
		}
		break;
	}
	}

	rt->target = CLAMP(rt->target, cfg->limit_lo, cfg->limit_hi);
}

/** Advance the post-resume gain ramp. */
static void zb_update_ramp(struct zb_joint_rt *rt, uint64_t now_us)
{
	if (!rt->ramping) {
		return;
	}

	if (ZB_RESUME_RAMP_US == 0ULL) {
		rt->gain_scale = 1.0f;
		rt->ramping = false;
		return;
	}

	uint64_t elapsed = now_us - rt->resume_start_us;

	if (elapsed >= ZB_RESUME_RAMP_US) {
		rt->gain_scale = 1.0f;
		rt->ramping = false;
	} else {
		rt->gain_scale = (float)elapsed / (float)ZB_RESUME_RAMP_US;
	}
}

void zb_control_step(struct zb_joint_rt *rt, uint8_t idx, uint64_t now_us, float dt)
{
	const struct zb_joint_cfg *cfg = rt->cfg;

	/* 1. Sample the plant. */
	zb_plant_read(idx, &rt->position, &rt->velocity);

	/* 2. Take the newest command, subject to the resume gate. */
	zb_take_command(rt, now_us);

	/*
	 * 3. Age of the last accepted command drives the mode. Note this keys
	 *    off command arrival, not link state: a host that holds its socket
	 *    open while publishing nothing ages out on the same schedule as one
	 *    whose cable was pulled.
	 */
	uint64_t age_us = rt->ever_accepted ? (now_us - rt->last_accept_us) : UINT64_MAX;
	enum zb_mode mode = zb_mode_for_age(age_us);

	/*
	 * Nothing is reset on a mode change, deliberately. The obvious thing to
	 * do on entering PARK is to clear the integrator, and it is wrong: the
	 * integrator is holding the gravity compensation, so dropping it steps
	 * the commanded effort by exactly that much at the worst moment. The
	 * target ramps down smoothly from where the hold left it and the
	 * integrator unwinds with it.
	 */
	rt->mode = mode;

	if (mode != ZB_MODE_PARK) {
		rt->parked = false;
	}

	/* 4. Where should the joint be right now? */
	zb_update_target(rt, mode, dt);

	/* 5. How much of the gains do we trust right now? */
	zb_update_ramp(rt, now_us);

	/*
	 * 6. Effort ceiling by mode. HOLDING and PARK are clamped to the hold
	 *    effort so that a joint nobody is talking to cannot draw full
	 *    torque indefinitely.
	 */
	float limit = (mode == ZB_MODE_FOLLOWING || mode == ZB_MODE_DEGRADED) ? cfg->max_effort
									      : cfg->hold_effort;

	/*
	 * 7. Latch the parked state once the descent has arrived and settled.
	 *    Latched, not recomputed, so that a joint drifting a millimetre
	 *    after the driver is cut does not re-energise it.
	 */
	if (mode == ZB_MODE_PARK && !rt->parked) {
		bool at_target = fabsf(rt->position - cfg->park_target) <= ZB_PARK_SETTLE_RAD;
		bool settled = fabsf(rt->velocity) <= ZB_PARK_SETTLE_RAD_S;
		bool ramp_done = rt->target == cfg->park_target;

		rt->parked = at_target && settled && ramp_done;
	}

	bool enable = !(mode == ZB_MODE_PARK && rt->parked);

	/* 8. PID. */
	float effort;

	if (!enable) {
		effort = 0.0f;
		rt->integrator = 0.0f;
	} else {
		float err = rt->target - rt->position;

		if (cfg->ki > 0.0f) {
			rt->integrator += err * dt;

			/* Anti-windup against the ceiling actually in force. */
			float imax = limit / cfg->ki;

			rt->integrator = CLAMP(rt->integrator, -imax, imax);
		} else {
			rt->integrator = 0.0f;
		}

		/*
		 * Derivative on measured velocity rather than on the error
		 * derivative. With derivative-on-error a step change in the
		 * setpoint produces an impulse in the output; here it produces
		 * nothing, which is most of what "bumpless" means in practice.
		 */
		effort = cfg->kp * err + cfg->ki * rt->integrator - cfg->kd * rt->velocity;
		effort *= rt->gain_scale;
		effort = CLAMP(effort, -limit, limit);
	}

	rt->effort = effort;
	rt->driver_enabled = enable;

	/* 9. Drive the plant, then let it advance by one timestep. */
	zb_plant_enable(idx, enable);
	zb_plant_apply_effort(idx, effort);
	zb_plant_update(idx, dt);
}
