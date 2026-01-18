/*
 * Copyright (c) 2025 Google LLC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM Cortex-M debug monitor functions interface based on DWT
 *
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_DEBUG_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_DEBUG_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Assess whether a debug monitor event should be treated as an error
 *
 * This routine checks the status of a debug_monitor() exception, and
 * evaluates whether this needs to be considered as a processor error.
 *
 * @return true if the DM exception is a processor error, otherwise false
 */
bool z_arm_debug_monitor_event_error_check(void);

/**
 * @brief Enable null pointer detection using debug monitor
 *
 * Configures the ARM Data Watchpoint and Trace (DWT) unit to detect
 * null pointer dereferences by setting up a watchpoint at address 0.
 * When enabled, any access to address 0 will trigger a debug monitor
 * exception.
 *
 * This feature is only available on Cortex-M processors with DWT support.
 *
 * @return 0 on success, negative error code on failure
 */
int z_arm_debug_enable_null_pointer_detection(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_DEBUG_H_ */
