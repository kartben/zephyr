/*
 * Copyright (c) 2019 - 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __STACK_SIZE_ANALYZER_H
#define __STACK_SIZE_ANALYZER_H

#include <stddef.h>
#include <zephyr/kernel/thread.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup thread_analyzer Thread analyzer
 *  @ingroup debug
 *  @brief Module for analyzing thread stack usage and performance
 *
 *  The thread analyzer provides comprehensive runtime analysis of thread
 *  behavior, including stack usage, CPU utilization, and performance metrics.
 *
 *  ## Stack Painting
 *
 *  Stack painting is a technique where thread stacks are initialized with a
 *  known pattern (0xaa) when CONFIG_INIT_STACKS is enabled. This allows the
 *  thread analyzer to detect stack usage by scanning from the bottom of the
 *  stack until it finds memory that has been modified. This mechanism provides
 *  accurate measurement of:
 *  - Current stack usage (how much stack is currently in use)
 *  - Unused stack space (pristine stack area with 0xaa pattern)
 *
 *  ## High Watermark Tracking
 *
 *  When CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK is enabled, the analyzer
 *  tracks the maximum stack usage observed for each thread since creation.
 *  This feature helps:
 *  - Identify worst-case stack usage patterns
 *  - Optimize stack size configurations
 *  - Detect intermittent stack-intensive operations
 *  - Ensure adequate stack margins for safety
 *
 *  The high watermark is stored in the thread's stack_info structure and
 *  persists across multiple analyzer runs, providing a historical view of
 *  peak stack utilization.
 *
 *  ## Usage
 *
 *  To use the thread analyzer:
 *  1. Enable CONFIG_THREAD_ANALYZER in your project configuration
 *  2. Enable CONFIG_INIT_STACKS for stack painting support
 *  3. Enable CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK for peak usage tracking
 *  4. Call thread_analyzer_print() or thread_analyzer_run() to analyze threads
 *
 *  Alternatively, enable CONFIG_THREAD_ANALYZER_AUTO to run analysis
 *  automatically at periodic intervals.
 *
 *  @{
 */

/**
 * @brief Thread analyzer information structure
 *
 * Contains detailed information about a thread's stack usage and
 * performance characteristics. This structure is populated by the
 * thread analyzer and passed to callback functions.
 */
struct thread_analyzer_info {
	/** Thread name or stringified thread handle address.
	 *
	 * If the thread has a name set via k_thread_name_set(), this field
	 * contains that name. Otherwise, it contains the hexadecimal string
	 * representation of the thread structure pointer.
	 */
	const char *name;

	/** Total stack size in bytes.
	 *
	 * Represents the total size of the stack buffer allocated for this
	 * thread, including any reserved areas and adjustments.
	 */
	size_t stack_size;

	/** Current stack usage in bytes.
	 *
	 * The amount of stack space currently used by the thread. This is
	 * calculated by scanning the stack for the initialized pattern (0xaa)
	 * when CONFIG_INIT_STACKS is enabled. The value represents the number
	 * of bytes that have been modified since thread creation.
	 */
	size_t stack_used;

#ifdef CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK
	/** High watermark - maximum stack usage in bytes.
	 *
	 * Tracks the peak stack utilization observed since thread creation.
	 * This field is only available when CONFIG_THREAD_ANALYZER_STACK_HIGH_WATERMARK
	 * is enabled. The high watermark is updated each time the thread analyzer
	 * runs if the current usage exceeds the previously recorded maximum.
	 *
	 * Use cases:
	 * - Identifying worst-case stack requirements
	 * - Optimizing stack size allocations
	 * - Detecting stack growth patterns over time
	 * - Ensuring adequate safety margins
	 *
	 * Note: Requires CONFIG_INIT_STACKS to be enabled for accurate measurement.
	 */
	size_t stack_high_watermark;
#endif

#ifdef CONFIG_THREAD_RUNTIME_STATS
	unsigned int utilization;
#ifdef CONFIG_SCHED_THREAD_USAGE
	k_thread_runtime_stats_t  usage;
#endif
#endif

#ifdef CONFIG_THREAD_ANALYZER_PRIV_STACK_USAGE
	/** Total size of privileged stack */
	size_t priv_stack_size;

	/** Privileged stack size in used */
	size_t priv_stack_used;
#endif
};

/** @brief Thread analyzer stack size callback function
 *
 *  Callback function with thread analysis information.
 *
 *  @param info Thread analysis information.
 */
typedef void (*thread_analyzer_cb)(struct thread_analyzer_info *info);

/** @brief Run the thread analyzer and provide information to the callback
 *
 *  This function analyzes the current state for all threads and calls
 *  a given callback on every thread found. In the special case when Kconfig
 *  option THREAD_ANALYZER_AUTO_SEPARATE_CORES is set, the function analyzes
 *  only the threads running on the specified cpu.
 *
 *  @param cb The callback function handler
 *  @param cpu cpu to analyze, ignored if THREAD_ANALYZER_AUTO_SEPARATE_CORES=n
 */
void thread_analyzer_run(thread_analyzer_cb cb, unsigned int cpu);

/** @brief Run the thread analyzer and print stack size statistics.
 *
 *  This function runs the thread analyzer and prints the output in
 *  standard form. In the special case when Kconfig option
 *  THREAD_ANALYZER_AUTO_SEPARATE_CORES is set, the function analyzes
 *  only the threads running on the specified cpu.
 *
 *  @param cpu cpu to analyze, ignored if THREAD_ANALYZER_AUTO_SEPARATE_CORES=n
 */
void thread_analyzer_print(unsigned int cpu);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* __STACK_SIZE_ANALYZER_H */
