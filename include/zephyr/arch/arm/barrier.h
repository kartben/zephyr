/**
 * Copyright (c) 2023 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 memory barrier operations
 *
 * This header provides ARM-specific implementations of memory barrier
 * operations. These functions are used to ensure proper ordering of
 * memory operations in multi-processor or memory-mapped I/O scenarios.
 *
 * @note This file should not be included directly. Include
 *       <zephyr/sys/barrier.h> instead.
 */

#ifndef ZEPHYR_INCLUDE_BARRIER_ARM_H_
#define ZEPHYR_INCLUDE_BARRIER_ARM_H_

#ifndef ZEPHYR_INCLUDE_SYS_BARRIER_H_
#error Please include <zephyr/sys/barrier.h>
#endif

#include <cmsis_core.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Full data memory barrier
 *
 * Executes a Data Memory Barrier (DMB) instruction to ensure that all
 * explicit memory accesses that appear in program order before the DMB
 * instruction are observed before any explicit memory accesses that
 * appear in program order after the DMB instruction.
 */
static ALWAYS_INLINE void z_barrier_dmem_fence_full(void)
{
	__DMB();
}

/**
 * @brief Full data synchronization barrier
 *
 * Executes a Data Synchronization Barrier (DSB) instruction to ensure
 * completion of all memory accesses and cache maintenance operations
 * before subsequent instructions execute.
 */
static ALWAYS_INLINE void z_barrier_dsync_fence_full(void)
{
	__DSB();
}

/**
 * @brief Full instruction synchronization barrier
 *
 * Executes an Instruction Synchronization Barrier (ISB) instruction to
 * flush the pipeline in the processor, ensuring that all instructions
 * following the ISB are fetched from cache or memory after the ISB
 * has been completed.
 */
static ALWAYS_INLINE void z_barrier_isync_fence_full(void)
{
	__ISB();
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_BARRIER_ARM_H_ */
