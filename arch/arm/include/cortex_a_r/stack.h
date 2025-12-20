/*
 * Copyright (c) 2018 Lexmark International, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Stack helpers for Cortex-A and Cortex-R CPUs
 *
 * Stack helper functions.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_AARCH32_CORTEX_A_R_STACK_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_AARCH32_CORTEX_A_R_STACK_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _ASMLANGUAGE

/* nothing */

#else

/**
 * @brief Initialize CPU stacks for different ARM modes
 *
 * This function initializes the stack pointers for the different ARM
 * processor modes (FIQ, IRQ, Abort, Undefined, SVC, System) on Cortex-A
 * and Cortex-R processors. Each mode has its own dedicated stack to ensure
 * proper exception handling.
 *
 * Must be called during early system initialization before exceptions
 * are enabled.
 */
extern void z_arm_init_stacks(void);

#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_AARCH32_CORTEX_A_R_STACK_H_ */
