/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kernel_shell.h"

#include <kernel_internal.h>

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>

struct thread_lookup_by_name {
	const char *name;
	struct k_thread *found;
};

static void find_by_name_cb(const struct k_thread *t, void *user_data)
{
	struct thread_lookup_by_name *ctx = user_data;
	const char *tname;

	if (ctx->found != NULL) {
		return;
	}

	tname = k_thread_name_get((struct k_thread *)t);
	if (tname != NULL && strcmp(tname, ctx->name) == 0) {
		ctx->found = (struct k_thread *)t;
	}
}

/*
 * Resolve a thread from either a hex address string (e.g. "0x20001234")
 * or a thread name string.  Returns NULL when not found.
 */
static struct k_thread *lookup_thread(const char *id_or_name)
{
	char *endptr;
	k_tid_t tid;
	struct thread_lookup_by_name ctx;

	/* Try hex address first */
	tid = (k_tid_t)(uintptr_t)strtoul(id_or_name, &endptr, 16);
	if (*endptr == '\0' && z_thread_is_valid(tid)) {
		return (struct k_thread *)tid;
	}

	/* Fall back to name search */
	ctx.name = id_or_name;
	ctx.found = NULL;
	k_thread_foreach(find_by_name_cb, &ctx);

	return ctx.found;
}

#ifdef CONFIG_THREAD_RUNTIME_STATS
static void inspect_rt_stats(const struct shell *sh, struct k_thread *thread)
{
	k_thread_runtime_stats_t rt_stats_thread;
	k_thread_runtime_stats_t rt_stats_all;
	unsigned int pcnt;

	if (k_thread_runtime_stats_get(thread, &rt_stats_thread) != 0 ||
	    k_thread_runtime_stats_all_get(&rt_stats_all) != 0) {
		shell_print(sh, "  runtime stats: unavailable");
		return;
	}

	pcnt = (rt_stats_all.execution_cycles > 0U)
		       ? (unsigned int)((rt_stats_thread.execution_cycles * 100U) /
					rt_stats_all.execution_cycles)
		       : 0U;
	shell_print(sh, "  runtime stats:");
	shell_print(sh, "    total execution cycles:   %u (%u %%)",
		    (uint32_t)rt_stats_thread.execution_cycles, pcnt);
#ifdef CONFIG_SCHED_THREAD_USAGE_ANALYSIS
	shell_print(sh, "    current execution cycles: %u",
		    (uint32_t)rt_stats_thread.current_cycles);
	shell_print(sh, "    peak execution cycles:    %u",
		    (uint32_t)rt_stats_thread.peak_cycles);
	shell_print(sh, "    average execution cycles: %u",
		    (uint32_t)rt_stats_thread.average_cycles);
#endif /* CONFIG_SCHED_THREAD_USAGE_ANALYSIS */
}
#endif /* CONFIG_THREAD_RUNTIME_STATS */

static int cmd_kernel_thread_inspect(const struct shell *sh, size_t argc, char **argv)
{
	struct k_thread *thread;
	const char *tname;
	char state_str[32];
	size_t size;
	size_t unused;
	unsigned int pcnt;
	int ret;

	ARG_UNUSED(argc);

	thread = lookup_thread(argv[1]);
	if (thread == NULL) {
		shell_error(sh, "Thread '%s' not found", argv[1]);
		return -ENOENT;
	}

	tname = k_thread_name_get(thread);
	size = thread->stack_info.size;

	shell_print(sh, "Thread %p  name: %s",
		    (void *)thread, tname ? tname : "(unnamed)");
	shell_print(sh, "  state:    %s",
		    k_thread_state_str(thread, state_str, sizeof(state_str)));
	shell_print(sh, "  priority: %d  options: 0x%x",
		    thread->base.prio, thread->base.user_options);
	shell_print(sh, "  entry:    %p", thread->entry.pEntry);
	shell_print(sh, "  timeout:  %" PRId64 " ticks",
		    (int64_t)thread->base.timeout.dticks);

#ifdef CONFIG_SCHED_CPU_MASK
	shell_print(sh, "  cpu mask: 0x%x", thread->base.cpu_mask);
#endif /* CONFIG_SCHED_CPU_MASK */

	ret = k_thread_stack_space_get(thread, &unused);
	if (ret != 0) {
		shell_print(sh, "  stack:    size %zu  (watermark unavailable)", size);
	} else {
		pcnt = (uint32_t)(((size - unused) * 100U) / size);
		shell_print(sh,
			    "  stack:    size %zu  used %zu / %zu (%u %%)  unused %zu",
			    size, size - unused, size, pcnt, unused);
	}

	IF_ENABLED(CONFIG_THREAD_RUNTIME_STATS, (inspect_rt_stats(sh, thread)));

	return 0;
}

KERNEL_THREAD_CMD_ARG_ADD(inspect, NULL,
			  "Show detailed info for a single thread.\n"
			  "Accepts a hex thread ID (e.g. 0x20001234) or a thread name.",
			  cmd_kernel_thread_inspect, 2, 0);
