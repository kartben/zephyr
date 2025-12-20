/*
 * Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
/**
 * @file
 * @brief Private kernel definitions (ARM)
 *
 * This file contains private kernel function definitions and various
 * other definitions for the 32-bit ARM Cortex-A/R processor architecture
 * family.
 *
 * This file is also included by assembly language files which must #define
 * _ASMLANGUAGE before including this header file.  Note that kernel
 * assembly source files obtains structure offset values via "absolute symbols"
 * in the offsets.o module.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_KERNEL_ARCH_FUNC_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_KERNEL_ARCH_FUNC_H_

#include <zephyr/platform/hooks.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE

/**
 * @brief Initialize ARM Cortex-A/R architecture-specific kernel features
 *
 * Performs architecture-specific initialization for Cortex-A and Cortex-R
 * processors. This includes calling SoC-specific per-core initialization
 * hooks if configured.
 *
 * This function is called during kernel initialization.
 */
static ALWAYS_INLINE void arch_kernel_init(void)
{
#ifdef CONFIG_SOC_PER_CORE_INIT_HOOK
	soc_per_core_init_hook();
#endif /* CONFIG_SOC_PER_CORE_INIT_HOOK */
}

#ifndef CONFIG_USE_SWITCH

/**
 * @brief Perform a context switch
 *
 * Triggers a context switch by invoking a supervisor call (SVC) and then
 * unlocking interrupts. The actual context switch occurs in the SVC handler.
 * This function is used when CONFIG_USE_SWITCH is not enabled.
 *
 * @param key Interrupt lock key to be restored after context switch
 * @return The return value set by arch_thread_return_value_set(), typically
 *         indicating the reason for the context switch
 */
static ALWAYS_INLINE int arch_swap(unsigned int key)
{
	/* store off key and return value */
	_current->arch.basepri = key;
	_current->arch.swap_return_value = -EAGAIN;

	z_arm_cortex_r_svc();
	irq_unlock(key);

	/* Context switch is performed here. Returning implies the
	 * thread has been context-switched-in again.
	 */
	return _current->arch.swap_return_value;
}

/**
 * @brief Set the return value for a thread
 *
 * Sets the return value that will be returned from arch_swap() when the
 * specified thread is switched in. This is used to communicate the reason
 * for the context switch to the thread.
 *
 * @param thread Thread structure to set the return value for
 * @param value Return value to be set
 */
static ALWAYS_INLINE void
arch_thread_return_value_set(struct k_thread *thread, unsigned int value)
{
	thread->arch.swap_return_value = value;
}

#else

/**
 * @brief Switch to a new thread context
 *
 * Performs a direct context switch to a new thread using the switch-based
 * context switch implementation (CONFIG_USE_SWITCH). This directly saves
 * and restores the context without going through an SVC exception.
 *
 * @param switch_to Pointer to the new thread to switch to
 * @param switched_from Pointer to store the pointer of the thread being
 *                      switched away from
 */
static ALWAYS_INLINE void arch_switch(void *switch_to, void **switched_from)
{
	extern void z_arm_context_switch(struct k_thread *new,
					struct k_thread *old);

	struct k_thread *new = switch_to;
	struct k_thread *old = CONTAINER_OF(switched_from, struct k_thread,
					    switch_handle);

	z_arm_context_switch(new, old);
}

#endif

/**
 * @brief Enter user mode and start executing a thread's entry point
 *
 * Transitions from privileged mode to unprivileged (user) mode and begins
 * executing the specified user thread entry function. This function does
 * not return.
 *
 * @param user_entry Entry point function of the user thread
 * @param p1 First parameter to pass to the entry function
 * @param p2 Second parameter to pass to the entry function
 * @param p3 Third parameter to pass to the entry function
 * @param stack_end End address of the thread's stack
 * @param stack_start Start address of the thread's stack
 * @param sp_is_priv Flag indicating if the stack pointer is privileged
 */
extern FUNC_NORETURN void z_arm_userspace_enter(k_thread_entry_t user_entry,
					       void *p1, void *p2, void *p3,
					       uint32_t stack_end,
					       uint32_t stack_start,
					       uint32_t sp_is_priv);

/**
 * @brief Handle fatal errors
 *
 * Handles fatal system errors by invoking the configured fatal error handler.
 * This function does not return and will halt or reset the system.
 *
 * @param reason Fatal error reason code
 * @param esf Exception stack frame containing processor state at the time
 *            of the error, may be NULL if not available
 */
extern void z_arm_fatal_error(unsigned int reason, const struct arch_esf *esf);

#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_KERNEL_ARCH_FUNC_H_ */
