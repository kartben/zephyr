/*
 * Copyright (c) 2015, Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 GCC specific public inline assembler functions and macros
 *
 * This header provides GCC/LLVM specific inline assembler functions and macros
 * for ARM AArch32 processors. These functions handle interrupt locking/unlocking
 * operations at the architecture level.
 *
 * @note This file must not be included directly. Include arch/cpu.h instead.
 */

/* Either public functions or macros or invoked by public functions */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_

/*
 * The file must not be included directly
 * Include arch/cpu.h instead
 */

#ifndef _ASMLANGUAGE

#include <zephyr/toolchain.h>
#include <zephyr/types.h>
#include <zephyr/arch/exception.h>
#include <cmsis_core.h>

#if defined(CONFIG_CPU_AARCH32_CORTEX_R) || defined(CONFIG_CPU_AARCH32_CORTEX_A)
#include <zephyr/arch/arm/cortex_a_r/cpu.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lock interrupts on the current CPU
 *
 * On ARMv7-M and ARMv8-M Mainline CPUs, this function prevents regular
 * exceptions (i.e. with interrupt priority lower than or equal to
 * _EXC_IRQ_DEFAULT_PRIO) from interrupting the CPU. NMI, Faults, SVC,
 * and Zero Latency IRQs (if supported) may still interrupt the CPU.
 *
 * On ARMv6-M and ARMv8-M Baseline CPUs, this function reads the value of
 * PRIMASK which shows if interrupts are enabled, then disables all interrupts
 * except NMI.
 *
 * On ARMv7-R, ARMv8-R, and ARMv7-A CPUs, this function saves the CPSR
 * interrupt bit and disables IRQs.
 *
 * @return An architecture-dependent lock-out key representing the
 *         interrupt state prior to the call.
 */
static ALWAYS_INLINE unsigned int arch_irq_lock(void)
{
	unsigned int key;

#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
#if CONFIG_MP_MAX_NUM_CPUS == 1 || defined(CONFIG_ARMV8_M_BASELINE)
	key = __get_PRIMASK();
	__disable_irq();
#else
#error "Cortex-M0 and Cortex-M0+ require SoC specific support for cross core synchronisation."
#endif
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	key = __get_BASEPRI();
	__set_BASEPRI_MAX(_EXC_IRQ_DEFAULT_PRIO);
	__ISB();
#elif defined(CONFIG_ARMV7_R) || defined(CONFIG_AARCH32_ARMV8_R) \
	|| defined(CONFIG_ARMV7_A)
	__asm__ volatile(
		"mrs %0, cpsr;"
		"and %0, #" STRINGIFY(I_BIT) ";"
		"cpsid i;"
		: "=r" (key)
		:
		: "memory", "cc");
#else
#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */

	return key;
}


/**
 * @brief Unlock interrupts on the current CPU
 *
 * Restores the interrupt state that was captured via a preceding invocation
 * of arch_irq_lock().
 *
 * On ARMv6-M and ARMv8-M Baseline CPUs, this enables all interrupts if they
 * were not previously disabled.
 *
 * On ARMv7-M and ARMv8-M Mainline CPUs, this restores the BASEPRI register.
 *
 * On ARMv7-R, ARMv8-R, and ARMv7-A CPUs, this enables IRQs if they were
 * previously enabled.
 *
 * @param key The lock-out key that was returned by a preceding invocation
 *            of arch_irq_lock().
 */
static ALWAYS_INLINE void arch_irq_unlock(unsigned int key)
{
#if defined(CONFIG_ARMV6_M_ARMV8_M_BASELINE)
	if (key != 0U) {
		return;
	}
	__enable_irq();
	__ISB();
#elif defined(CONFIG_ARMV7_M_ARMV8_M_MAINLINE)
	__set_BASEPRI(key);
	__ISB();
#elif defined(CONFIG_ARMV7_R) || defined(CONFIG_AARCH32_ARMV8_R) \
	|| defined(CONFIG_ARMV7_A)
	if (key != 0U) {
		return;
	}
	__enable_irq();
#else
#error Unknown ARM architecture
#endif /* CONFIG_ARMV6_M_ARMV8_M_BASELINE */
}

/**
 * @brief Test if calling arch_irq_unlock() with this key would unlock irqs
 *
 * Checks if the key value represents an unlocked state. This convention
 * works for both PRIMASK (on ARMv6-M/ARMv8-M Baseline) and BASEPRI
 * (on ARMv7-M/ARMv8-M Mainline).
 *
 * @param key The lock-out key to test.
 *
 * @return true if interrupts would be unlocked, false otherwise.
 */
static ALWAYS_INLINE bool arch_irq_unlocked(unsigned int key)
{
	/* This convention works for both PRIMASK and BASEPRI */
	return key == 0U;
}

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_ASM_INLINE_GCC_H_ */
