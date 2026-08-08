/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZENBEDDED_CONTROL_H_
#define ZENBEDDED_CONTROL_H_

#include <stdbool.h>
#include <stdint.h>

#include <zenbedded/hist.h>
#include <zenbedded/joint.h>
#include <zenbedded/setpoint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Failover mode.
 *
 * Owned entirely by the MCU. Nothing here waits for the host to admit there is
 * a problem. The clock runs off command *arrival*, not link state, so a wedged
 * host that holds its TCP connection open while publishing nothing is caught on
 * exactly the same schedule as a yanked cable.
 *
 * Wire-visible: transmitted as the "mode" byte of struct zb_state.
 */
enum zb_mode {
	/** Age < CONFIG_ZENBEDDED_AGE_DEGRADED_MS. Track the command, full gains. */
	ZB_MODE_FOLLOWING = 0,
	/** Hold the last valid setpoint, full gains. */
	ZB_MODE_DEGRADED = 1,
	/** Hold, effort clamped to hold-effort-mnm. */
	ZB_MODE_HOLDING = 2,
	/** Rate-limited descent to the park target, then driver disable. */
	ZB_MODE_PARK = 3,
};

/** Human-readable mode name. Never called from the control thread. */
const char *zb_mode_name(enum zb_mode mode);

/**
 * @brief Per-joint runtime state.
 *
 * Owned by the control thread. The comms thread touches exactly one field of
 * this structure, @ref zb_joint_rt.sp, and only through the wait-free channel
 * API. There is no lock here because there is nothing to lock: no mutex, no
 * semaphore, no object both threads can hold.
 */
struct zb_joint_rt {
	const struct zb_joint_cfg *cfg;

	/** The only cross-thread channel. Comms writes, control reads. */
	struct zb_sp_chan sp;

	/** Last command that passed validation and the resume gate. */
	struct zb_setpoint active;
	/** Device time at which @ref active was accepted, microseconds. */
	uint64_t last_accept_us;
	/** False until the first command is ever accepted. */
	bool ever_accepted;

	/** Current failover mode. */
	enum zb_mode mode;

	/** Position the PID is currently driving to, radians. */
	float target;
	/** PID integrator, radian-seconds. */
	float integrator;

	/** Gain multiplier, 0..1, ramped after a resume. */
	float gain_scale;
	/** Device time at which the resume ramp started, microseconds. */
	uint64_t resume_start_us;
	/** True while the gain ramp is in progress. */
	bool ramping;

	/** Driver enable state. False means the joint is coasting. */
	bool driver_enabled;
	/**
	 * Latched once the PARK descent has arrived and settled. Latched rather
	 * than recomputed each step so that a joint drifting slightly after the
	 * driver is cut cannot re-energise it.
	 */
	bool parked;

	/** Last commanded effort, newton-metres. */
	float effort;
	/** Last sampled position and velocity. */
	float position;
	float velocity;

	/** Commands rejected by the resume gate. */
	uint32_t rejected_cmds;
	/** Commands accepted. */
	uint32_t accepted_cmds;
};

/**
 * @brief Initialise runtime state for joint @p idx.
 *
 * Leaves the joint in PARK with the driver disabled, which is where a joint
 * that has never heard from a host belongs.
 */
void zb_joint_rt_init(struct zb_joint_rt *rt, uint8_t idx);

/**
 * @brief Run one control iteration.
 *
 * Every path through this function is straight-line and constant-time. There
 * are no unbounded loops, no allocation, no logging, no network calls and
 * nothing that can block.
 *
 * @param rt     joint runtime state
 * @param idx    joint index, for the plant backend
 * @param now_us device time, microseconds
 * @param dt     timestep in seconds
 *
 * Exposed rather than hidden inside the thread so that tests can drive it in
 * virtual time and get bit-identical results on every run.
 */
void zb_control_step(struct zb_joint_rt *rt, uint8_t idx, uint64_t now_us, float dt);

/** Runtime state of every joint. Read-only outside the control thread. */
extern struct zb_joint_rt zb_rt[ZB_JOINT_COUNT];

/** Loop period histogram. */
extern struct zb_hist zb_loop_hist;

/**
 * @brief Start the 1 kHz control thread.
 *
 * Called automatically at boot unless CONFIG_ZENBEDDED_CONTROL_AUTOSTART is
 * disabled.
 */
void zb_control_start(void);

#ifdef __cplusplus
}
#endif

#endif /* ZENBEDDED_CONTROL_H_ */
