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
 * other definitions for the 32-bit ARM Cortex-M processor architecture
 * family.
 *
 * This file is also included by assembly language files which must #define
 * _ASMLANGUAGE before including this header file.  Note that kernel
 * assembly source files obtains structure offset values via "absolute symbols"
 * in the offsets.o module.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_KERNEL_ARCH_FUNC_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_KERNEL_ARCH_FUNC_H_

#include <zephyr/platform/hooks.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE
/**
 * @brief Initialize ARM fault handling
 *
 * Initializes the fault handling subsystem for ARM Cortex-M processors,
 * including configuration of fault exception handlers.
 */
extern void z_arm_fault_init(void);

/**
 * @brief Initialize CPU idle mode support
 *
 * Initializes any architecture-specific features required for CPU idle
 * modes on ARM Cortex-M processors.
 */
extern void z_arm_cpu_idle_init(void);

#ifdef CONFIG_ARM_MPU
/**
 * @brief Configure static MPU regions
 *
 * Programs the MPU with static memory regions defined at compile time.
 * These regions establish the base memory protection configuration for
 * the system.
 */
extern void z_arm_configure_static_mpu_regions(void);

/**
 * @brief Configure dynamic MPU regions for a thread
 *
 * Programs thread-specific MPU regions such as thread stack and memory
 * domain partitions. Called during thread context switches.
 *
 * @param thread Thread to configure MPU regions for
 */
extern void z_arm_configure_dynamic_mpu_regions(struct k_thread *thread);

/**
 * @brief Initialize the MPU
 *
 * Performs initial MPU hardware configuration and enables the MPU.
 *
 * @return 0 on success, negative error code on failure
 */
extern int z_arm_mpu_init(void);
#endif /* CONFIG_ARM_MPU */

#ifdef CONFIG_ARM_AARCH32_MMU
/**
 * @brief Initialize the MMU
 *
 * Performs initial MMU hardware configuration including page table setup
 * and enables the MMU.
 *
 * @return 0 on success, negative error code on failure
 */
extern int z_arm_mmu_init(void);
#endif /* CONFIG_ARM_AARCH32_MMU */

/**
 * @brief Initialize ARM Cortex-M architecture-specific kernel features
 *
 * Performs architecture-specific initialization for Cortex-M processors,
 * including interrupt stack setup, exception configuration, fault handling,
 * MPU/MMU initialization, and SoC-specific hooks.
 *
 * This function is called during kernel initialization.
 */
static ALWAYS_INLINE void arch_kernel_init(void)
{
	z_arm_interrupt_stack_setup();
	z_arm_exc_setup();
	z_arm_fault_init();
	z_arm_cpu_idle_init();
	z_arm_clear_faults();
#if defined(CONFIG_ARM_MPU)
	z_arm_mpu_init();
	/* Configure static memory map. This will program MPU regions,
	 * to set up access permissions for fixed memory sections, such
	 * as Application Memory or No-Cacheable SRAM area.
	 *
	 * This function is invoked once, upon system initialization.
	 */
	z_arm_configure_static_mpu_regions();
#endif /* CONFIG_ARM_MPU */

#ifdef CONFIG_SOC_PER_CORE_INIT_HOOK
	soc_per_core_init_hook();
#endif /* CONFIG_SOC_PER_CORE_INIT_HOOK */
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
static ALWAYS_INLINE void arch_thread_return_value_set(struct k_thread *thread, unsigned int value)
{
	thread->arch.swap_return_value = value;
}

#if !defined(CONFIG_MULTITHREADING)
/**
 * @brief Switch to main thread without multithreading support
 *
 * Switches to the main application thread when multithreading is disabled.
 * This function does not return.
 *
 * @param main_func Main thread entry point function
 * @param p1 First parameter to pass to main_func
 * @param p2 Second parameter to pass to main_func
 * @param p3 Third parameter to pass to main_func
 */
extern FUNC_NORETURN void z_arm_switch_to_main_no_multithreading(k_thread_entry_t main_func,
								 void *p1, void *p2, void *p3);

#define ARCH_SWITCH_TO_MAIN_NO_MULTITHREADING z_arm_switch_to_main_no_multithreading

#endif /* !CONFIG_MULTITHREADING */

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
extern FUNC_NORETURN void z_arm_userspace_enter(k_thread_entry_t user_entry, void *p1, void *p2,
						void *p3, uint32_t stack_end, uint32_t stack_start,
						uint32_t sp_is_priv);

/**
 * @brief Handle fatal errors
 *
 * Handles fatal system errors by invoking the configured fatal error handler.
 * This function does not return and will halt or reset the system.
 *
 * @param reason Fatal error reason code
 * @param esf Exception stack frame containing processor state at the time
 *            of the error
 */
extern void z_arm_fatal_error(unsigned int reason, const struct arch_esf *esf);

/**
 * @brief Perform a context switch
 *
 * Triggers a context switch by setting the PendSV exception pending and
 * enabling interrupts. The actual context switch occurs in the PendSV
 * exception handler.
 *
 * @param key Interrupt lock key to be saved and restored
 * @return The return value set by arch_thread_return_value_set(), typically
 *         indicating the reason for the context switch
 */
static ALWAYS_INLINE int arch_swap(unsigned int key)
{
	/* store off key and return value */
	_current->arch.basepri = key;
	_current->arch.swap_return_value = -EAGAIN;

	/* set pending bit to make sure we will take a PendSV exception */
	SCB->ICSR |= SCB_ICSR_PENDSVSET_Msk;

	/* clear mask or enable all irqs to take a pendsv */
	irq_unlock(0);

	/* Context switch is performed here. Returning implies the
	 * thread has been context-switched-in again.
	 */
	return _current->arch.swap_return_value;
}

#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_CORTEX_M_KERNEL_ARCH_FUNC_H_ */
