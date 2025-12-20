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
 * @brief Get 32-bit system clock cycle count
 *
 * Reads the current cycle counter value. Implementation is platform-specific.
 *
 * @return Current 32-bit cycle count
 */
extern uint32_t sys_clock_cycle_get_32(void);

/**
 * @brief Get 32-bit cycle count
 *
 * Architecture-specific wrapper to get the current cycle count.
 *
 * @return Current 32-bit cycle count
 */
static inline uint32_t arch_k_cycle_get_32(void)
{
	return sys_clock_cycle_get_32();
}

/**
 * @brief Get 64-bit system clock cycle count
 *
 * Reads the current cycle counter value as a 64-bit value. Implementation
 * is platform-specific.
 *
 * @return Current 64-bit cycle count
 */
extern uint64_t sys_clock_cycle_get_64(void);

/**
 * @brief Get 64-bit cycle count
 *
 * Architecture-specific wrapper to get the current cycle count.
 *
 * @return Current 64-bit cycle count
 */
static inline uint64_t arch_k_cycle_get_64(void)
{
	return sys_clock_cycle_get_64();
}

/**
 * @brief Execute a no-operation instruction
 *
 * Inserts an ARM NOP instruction, which does nothing but consume one cycle.
 */
static ALWAYS_INLINE void arch_nop(void)
{
	__asm__ volatile("nop");
}

#if defined(CONFIG_USERSPACE)
/**
 * @brief Check if thread is in user mode
 *
 * Determines if the current thread is running in unprivileged user mode.
 *
 * @return true if in user mode, false if in privileged mode
 */
extern bool z_arm_thread_is_in_user_mode(void);
#endif

#if defined(CONFIG_ARM_ON_ENTER_CPU_IDLE_HOOK)
/* Prototype of a hook that can be enabled to be called every time the CPU is
 * made idle (the calls will be done from k_cpu_idle() and k_cpu_atomic_idle()).
 * If this hook returns false, the CPU is prevented from entering the actual
 * sleep (the WFE/WFI instruction is skipped).
 */
bool z_arm_on_enter_cpu_idle(void);
#endif

#if defined(CONFIG_ARM_ON_ENTER_CPU_IDLE_PREPARE_HOOK)
/* Prototype of a hook that can be enabled to be called every time the CPU is
 * made idle (the calls will be done from k_cpu_idle() and k_cpu_atomic_idle()).
 * The function is called before interrupts are disabled and can prepare to
 * upcoming call to z_arm_on_enter_cpu_idle.
 */
void z_arm_on_enter_cpu_idle_prepare(void);
#endif

#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_MISC_H_ */
