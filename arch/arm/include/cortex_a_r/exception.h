/*
 * Copyright (c) 2018 Lexmark International, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Exception/interrupt context helpers for Cortex-A and Cortex-R CPUs
 *
 * Exception/interrupt context helpers.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_EXCEPTION_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_EXCEPTION_H_

#include <zephyr/arch/cpu.h>

#ifdef _ASMLANGUAGE

/* nothing */

#else

#include <zephyr/irq_offload.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef CONFIG_IRQ_OFFLOAD
extern volatile irq_offload_routine_t offload_routine;
#endif

/**
 * @brief Check if running in interrupt context
 *
 * Determines if the CPU is currently executing in interrupt context by
 * checking the nested exception counter. For Cortex-A/R processors, this
 * is tracked by the per-CPU nested field.
 *
 * @return true if in interrupt context, false otherwise
 */
static ALWAYS_INLINE bool arch_is_in_isr(void)
{
	uint32_t nested;
#ifdef CONFIG_SMP
	unsigned int key;

	key = arch_irq_lock();
#endif
	nested = arch_curr_cpu()->nested;
#ifdef CONFIG_SMP
	arch_irq_unlock(key);
#endif
	return nested != 0U;
}

/**
 * @brief Check if a nested exception occurred
 *
 * Determines if the current exception was triggered while already handling
 * another exception (nested exception). This is determined by checking the
 * exception depth counter.
 *
 * @param esf Exception stack frame (unused in this implementation)
 * @return true if nested exception, false otherwise
 */
static ALWAYS_INLINE bool arch_is_in_nested_exception(const struct arch_esf *esf)
{
	return (_current_cpu->arch.exc_depth > 1U) ? (true) : (false);
}

/**
 * @brief No current implementation where core dump is not supported
 *
 * @param esf exception frame
 * @param exc_return EXC_RETURN value present in LR after exception entry.
 */
static ALWAYS_INLINE void z_arm_set_fault_sp(const struct arch_esf *esf, uint32_t exc_return)
{}

#if defined(CONFIG_USERSPACE)
/**
 * @brief Check if preempted thread was in user mode
 *
 * Determines if the thread that was preempted by the current exception was
 * running in unprivileged (user) mode by examining the saved processor status
 * register (xPSR) in the exception stack frame.
 *
 * @param esf Exception stack frame containing saved processor state
 * @return true if the preempted thread was in user mode, false otherwise
 */
static ALWAYS_INLINE bool z_arm_preempted_thread_in_user_mode(const struct arch_esf *esf)
{
	return ((esf->basic.xpsr & CPSR_M_Msk) == CPSR_M_USR);
}
#endif

#ifndef CONFIG_USE_SWITCH
/**
 * @brief Supervisor call handler for context switching
 *
 * Handles supervisor call (SVC) exceptions used for voluntary context
 * switches on Cortex-R processors when not using the switch-based
 * context switch implementation.
 */
extern void z_arm_cortex_r_svc(void);
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_A_R_EXCEPTION_H_ */
