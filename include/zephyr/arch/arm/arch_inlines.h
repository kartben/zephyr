/*
 * Copyright 2022 IoT.bzh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 architecture inline function implementations
 *
 * This header provides architecture-specific inline functions for ARM AArch32
 * processors. These functions provide core CPU information and are used
 * internally by the kernel.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_ARCH_INLINES_H
#define ZEPHYR_INCLUDE_ARCH_ARM_ARCH_INLINES_H

#include <zephyr/kernel_structs.h>
#if defined(CONFIG_CPU_AARCH32_CORTEX_R) || defined(CONFIG_CPU_AARCH32_CORTEX_A)
#include <zephyr/arch/arm/cortex_a_r/lib_helpers.h>
#include <zephyr/arch/arm/cortex_a_r/tpidruro.h>

/**
 * @brief Get the current CPU structure pointer
 *
 * Returns a pointer to the _cpu_t structure for the CPU on which the
 * caller is executing. On Cortex-A/R processors, this is retrieved
 * from the TPIDRURO register.
 *
 * @return Pointer to the current CPU's _cpu_t structure.
 */
static ALWAYS_INLINE _cpu_t *arch_curr_cpu(void)
{
	return (_cpu_t *)(read_tpidruro() & TPIDRURO_CURR_CPU);
}
#else

#ifndef CONFIG_SMP
/**
 * @brief Get the current CPU structure pointer
 *
 * Returns a pointer to the _cpu_t structure for the CPU on which the
 * caller is executing. For non-SMP configurations on Cortex-M, this
 * always returns the first CPU.
 *
 * @return Pointer to the current CPU's _cpu_t structure.
 */
static ALWAYS_INLINE _cpu_t *arch_curr_cpu(void)
{
	/* Dummy implementation always return the first cpu */
	return &_kernel.cpus[0];
}
#endif
#endif

/**
 * @brief Get the current processor ID
 *
 * Returns the processor ID of the CPU on which the caller is executing.
 * This is a placeholder implementation that retrieves the ID from the
 * current CPU structure.
 *
 * @return The processor ID.
 */
static ALWAYS_INLINE uint32_t arch_proc_id(void)
{
	/*
	 * Placeholder implementation to be replaced with an architecture
	 * specific call to get processor ID
	 */
	return arch_curr_cpu()->id;
}

/**
 * @brief Get the number of CPUs in the system
 *
 * Returns the maximum number of CPUs configured for the system.
 *
 * @return The number of CPUs.
 */
static ALWAYS_INLINE unsigned int arch_num_cpus(void)
{
	return CONFIG_MP_MAX_NUM_CPUS;
}

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_ARCH_INLINES_H */
