/**
 * @file debug/stack.h
 * Stack usage analysis helpers
 * @ingroup debug
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DEBUG_STACK_H_
#define ZEPHYR_INCLUDE_DEBUG_STACK_H_

#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/toolchain.h>
#include <stdbool.h>

/**
 * @brief Log the stack usage of a thread.
 *
 * Log the unused and used amount of the stack of @p thread, along with
 * the usage percentage, through the "os" logging module at INFO level.
 *
 * Does nothing unless @kconfig{CONFIG_INIT_STACKS} and
 * @kconfig{CONFIG_THREAD_STACK_INFO} are enabled.
 *
 * @param thread Thread whose stack usage is logged.
 */
static inline void log_stack_usage(const struct k_thread *thread)
{
#if defined(CONFIG_INIT_STACKS) && defined(CONFIG_THREAD_STACK_INFO)
	size_t unused, size = thread->stack_info.size;

	TOOLCHAIN_DISABLE_WARNING(TOOLCHAIN_WARNING_SHADOW);
	LOG_MODULE_DECLARE(os, CONFIG_KERNEL_LOG_LEVEL);
	TOOLCHAIN_ENABLE_WARNING(TOOLCHAIN_WARNING_SHADOW);

	if (k_thread_stack_space_get(thread, &unused) == 0) {
		unsigned int pcnt = ((size - unused) * 100U) / size;
		const char *tname;

		tname = k_thread_name_get((k_tid_t)thread);
		if (tname == NULL) {
			tname = "unknown";
		}

		LOG_INF("%p (%s):\tunused %zu\tusage %zu / %zu (%u %%)",
			thread, tname, unused, size - unused, size,
			pcnt);
	}
#else
	ARG_UNUSED(thread);
#endif
}
#endif /* ZEPHYR_INCLUDE_DEBUG_STACK_H_ */
