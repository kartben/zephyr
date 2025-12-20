/*
 * Copyright (c) 2013-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 public kernel miscellaneous
 *
 * ARM AArch32-specific kernel miscellaneous interface. Included by arm/arch.h.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_MISC_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_MISC_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef _ASMLANGUAGE

/**
 * @brief Get the current 32-bit hardware cycle count
 *
 * Returns the current value of the hardware cycle counter.
 *
 * @return Current 32-bit cycle count.
 */
extern uint32_t sys_clock_cycle_get_32(void);

/**
 * @brief Architecture-specific 32-bit cycle count accessor
 *
 * Returns the current 32-bit hardware cycle count by calling the
 * system clock cycle counter.
 *
 * @return Current 32-bit cycle count.
 */
static inline uint32_t arch_k_cycle_get_32(void)
{
	return sys_clock_cycle_get_32();
}

/**
 * @brief Get the current 64-bit hardware cycle count
 *
 * Returns the current value of the hardware cycle counter as a 64-bit value.
 *
 * @return Current 64-bit cycle count.
 */
extern uint64_t sys_clock_cycle_get_64(void);

/**
 * @brief Architecture-specific 64-bit cycle count accessor
 *
 * Returns the current 64-bit hardware cycle count by calling the
 * system clock cycle counter.
 *
 * @return Current 64-bit cycle count.
 */
static inline uint64_t arch_k_cycle_get_64(void)
{
	return sys_clock_cycle_get_64();
}

/**
 * @brief Execute a NOP instruction
 *
 * Executes a single no-operation instruction. This can be used for
 * timing loops or as a placeholder in code.
 */
static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile("nop");
}

#if defined(CONFIG_USERSPACE)
/**
 * @brief Check if the current thread is in user mode
 *
 * Determines whether the currently executing thread is running
 * in unprivileged user mode.
 *
 * @return true if in user mode, false if in privileged mode.
 */
extern bool z_arm_thread_is_in_user_mode(void);
#endif

#if defined(CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK)
/**
 * @brief Hook called before entering CPU idle state
 *
 * This function is called every time the CPU is about to enter idle state
 * (from k_cpu_idle() and k_cpu_atomic_idle()). If this hook returns false,
 * the CPU is prevented from entering the actual sleep state (the WFE/WFI
 * instruction is skipped).
 *
 * @return true to allow entering idle, false to prevent idle entry.
 */
bool z_arm_on_enter_cpu_idle(void);
#endif

#if defined(CONFIG_ARM_ON_ENTER_CPU_IDLE_PREPARE_HOOK)
/**
 * @brief Prepare hook called before entering CPU idle state
 *
 * This function is called every time the CPU is about to enter idle state
 * (from k_cpu_idle() and k_cpu_atomic_idle()). The function is called
 * before interrupts are disabled and can prepare for the upcoming call
 * to z_arm_on_enter_cpu_idle().
 */
void z_arm_on_enter_cpu_idle_prepare(void);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_MISC_H_ */
