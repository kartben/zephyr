/*
 * Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM-specific kernel structure member offset definitions
 *
 * This header contains shorthand definitions for offsets into ARM-specific
 * thread and CPU structures. These offsets are primarily used in assembly
 * code to access structure members directly without the overhead of C code.
 *
 * The offsets are derived from the auto-generated offsets.h file which is
 * created during the build process by analyzing the actual structure layouts.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_OFFSETS_SHORT_ARCH_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_OFFSETS_SHORT_ARCH_H_

#include <zephyr/offsets.h>

/* kernel */

/* nothing for now */

/* end - kernel */

/* threads */

/**
 * @brief Offset to BASEPRI register value in thread structure
 *
 * Used to save and restore the BASEPRI register during context switches
 * on Cortex-M processors with BASEPRI support.
 */
#define _thread_offset_to_basepri \
	(___thread_t_arch_OFFSET + ___thread_arch_t_basepri_OFFSET)

/**
 * @brief Offset to preemptible floating point flag in thread structure
 *
 * Indicates whether floating point context can be preempted for this thread.
 */
#define _thread_offset_to_preempt_float \
	(___thread_t_arch_OFFSET + ___thread_arch_t_preempt_float_OFFSET)

#if defined(CONFIG_CPU_AARCH32_CORTEX_A) || defined(CONFIG_CPU_AARCH32_CORTEX_R)
/**
 * @brief Offset to exception nesting depth in thread structure
 *
 * Tracks the current exception nesting level for Cortex-A/R processors.
 */
#define _thread_offset_to_exception_depth \
	(___thread_t_arch_OFFSET + ___thread_arch_t_exception_depth_OFFSET)

/**
 * @brief Offset to exception depth in CPU structure
 *
 * Tracks the per-CPU exception nesting depth for Cortex-A/R processors.
 */
#define _cpu_offset_to_exc_depth \
	(___cpu_t_arch_OFFSET + ___cpu_arch_t_exc_depth_OFFSET)
#endif

#if defined(CONFIG_USERSPACE) || defined(CONFIG_FPU_SHARING)
/**
 * @brief Offset to processor mode field in thread structure
 *
 * Stores the processor mode (user/privileged) for the thread. Used by
 * both userspace and FPU sharing features.
 */
#define _thread_offset_to_mode \
	(___thread_t_arch_OFFSET + ___thread_arch_t_mode_OFFSET)
#endif

#if defined(CONFIG_ARM_STORE_EXC_RETURN)
/**
 * @brief Offset to exception return value in thread structure
 *
 * Stores the EXC_RETURN value which determines the context to return to
 * after an exception on Cortex-M processors.
 */
#define _thread_offset_to_mode_exc_return \
	(___thread_t_arch_OFFSET + ___thread_arch_t_mode_exc_return_OFFSET)
#endif

#ifdef CONFIG_USERSPACE
/**
 * @brief Offset to privileged stack start address in thread structure
 *
 * Points to the beginning of the privileged mode stack for user threads.
 */
#define _thread_offset_to_priv_stack_start \
	(___thread_t_arch_OFFSET + ___thread_arch_t_priv_stack_start_OFFSET)

/**
 * @brief Offset to privileged stack end address in thread structure
 *
 * Points to the end of the privileged mode stack for user threads.
 */
#define _thread_offset_to_priv_stack_end \
	(___thread_t_arch_OFFSET + ___thread_arch_t_priv_stack_end_OFFSET)

#if defined(CONFIG_CPU_AARCH32_CORTEX_R)
/**
 * @brief Offset to user mode stack pointer in thread structure
 *
 * Stores the user mode stack pointer (SP_usr) for Cortex-R processors
 * when using userspace.
 */
#define _thread_offset_to_sp_usr \
	(___thread_t_arch_OFFSET + ___thread_arch_t_sp_usr_OFFSET)
#endif
#endif

#if defined(CONFIG_THREAD_STACK_INFO)
/**
 * @brief Offset to stack info start address in thread structure
 *
 * Points to the start of the stack information structure which contains
 * stack boundaries for overflow detection.
 */
#define _thread_offset_to_stack_info_start \
	(___thread_stack_info_t_start_OFFSET + ___thread_t_stack_info_OFFSET)
#endif

#if defined(CONFIG_ARM_PAC_PER_THREAD)
/**
 * @brief Offset to Pointer Authentication Code keys in thread structure
 *
 * Stores the per-thread PAC keys used for pointer authentication on
 * ARM processors with PAC support.
 */
#define _thread_offset_to_pac_keys \
	(___thread_t_arch_OFFSET + ___thread_arch_t_pac_keys_OFFSET)
#endif

/* end - threads */

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_OFFSETS_SHORT_ARCH_H_ */
