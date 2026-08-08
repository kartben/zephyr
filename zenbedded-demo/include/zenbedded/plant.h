/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZENBEDDED_PLANT_H_
#define ZENBEDDED_PLANT_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal plant API.
 *
 * The simulated and hardware backends implement this identically, so the
 * control core, the failover state machine and every test above this line are
 * unaware of which one they are driving.
 *
 * These are plain functions, not a vtable, and the backend is chosen by
 * Kconfig at compile time. The control loop's worst case has to be inspectable,
 * and an indirect call through a pointer that could in principle be anything is
 * not.
 *
 * All units are float SI: radians, radians per second, newton-metres.
 */

/** Initialise every joint's plant. Called once, before the control thread runs. */
void zb_plant_init(void);

/** Enable or disable the driver. A disabled joint coasts. */
void zb_plant_enable(uint8_t idx, bool enable);

/** Command effort, newton-metres. Ignored while the driver is disabled. */
void zb_plant_apply_effort(uint8_t idx, float effort);

/** Sample position (radians) and velocity (radians per second). */
void zb_plant_read(uint8_t idx, float *position, float *velocity);

/**
 * @brief Advance the plant by @p dt seconds.
 *
 * The simulated backend integrates here. The hardware backend does nothing:
 * physics does not need to be asked. Called at the end of every control step so
 * that both backends see the same call sequence.
 */
void zb_plant_update(uint8_t idx, float dt);

#ifdef CONFIG_ZENBEDDED_PLANT_SIM
/**
 * @brief Force the simulated plant to a known state.
 *
 * Test hook. Not part of the backend-independent API, and deliberately absent
 * on hardware, where there is no such thing as teleporting a joint.
 */
void zb_plant_sim_set_state(uint8_t idx, float position, float velocity);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZENBEDDED_PLANT_H_ */
