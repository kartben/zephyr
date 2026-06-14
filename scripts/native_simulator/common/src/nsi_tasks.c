/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdbool.h>
#include "nsi_tasks.h"

#if defined(__APPLE__)

#include <stddef.h>
#include "nsi_host_sections.h"

/**
 * @brief Run the set of special NSI tasks corresponding to the given level
 *
 * macOS variant: all tasks are collected from a single Mach-O section without
 * any link-time ordering, so we select the entries for this level and run them
 * in increasing priority order, mirroring what the GNU ld linker script
 * produces on other hosts.
 *
 * @param level One of NSITASK_*_LEVEL as defined in nsi_tasks.h
 */
void nsi_run_tasks(int level)
{
	struct nsi_task_entry *tasks;
	size_t n_tasks;

	NSI_HOST_GET_SECTION(struct nsi_task_entry, "__nsi_task", tasks, n_tasks);

	/* Find the lowest not-yet-run priority among entries of this level, run all
	 * entries with that priority (in definition order), and repeat. n_tasks is
	 * small so the quadratic scan is irrelevant.
	 */
	int last_prio = -1;

	while (true) {
		int next_prio = -1;

		for (size_t i = 0; i < n_tasks; i++) {
			if (tasks[i].level != level) {
				continue;
			}
			if ((tasks[i].prio > last_prio) &&
			    ((next_prio == -1) || (tasks[i].prio < next_prio))) {
				next_prio = tasks[i].prio;
			}
		}

		if (next_prio == -1) {
			break;
		}

		for (size_t i = 0; i < n_tasks; i++) {
			if ((tasks[i].level == level) && (tasks[i].prio == next_prio) &&
			    (tasks[i].fn != NULL)) {
				tasks[i].fn();
			}
		}

		last_prio = next_prio;
	}
}

#else /* !defined(__APPLE__) */

/**
 * @brief Run the set of special NSI tasks corresponding to the given level
 *
 * @param level One of NSITASK_*_LEVEL as defined in nsi_tasks.h
 */
void nsi_run_tasks(int level)
{
	extern void (*__nsi_PRE_BOOT_1_tasks_start[])(void);
	extern void (*__nsi_PRE_BOOT_2_tasks_start[])(void);
	extern void (*__nsi_HW_INIT_tasks_start[])(void);
	extern void (*__nsi_PRE_BOOT_3_tasks_start[])(void);
	extern void (*__nsi_FIRST_SLEEP_tasks_start[])(void);
	extern void (*__nsi_ON_EXIT_PRE_tasks_start[])(void);
	extern void (*__nsi_ON_EXIT_POST_tasks_start[])(void);
	extern void (*__nsi_tasks_end[])(void);

	static void (**nsi_pre_tasks[])(void) = {
		__nsi_PRE_BOOT_1_tasks_start,
		__nsi_PRE_BOOT_2_tasks_start,
		__nsi_HW_INIT_tasks_start,
		__nsi_PRE_BOOT_3_tasks_start,
		__nsi_FIRST_SLEEP_tasks_start,
		__nsi_ON_EXIT_PRE_tasks_start,
		__nsi_ON_EXIT_POST_tasks_start,
		__nsi_tasks_end
	};

	void (**fptr)(void);

	for (fptr = nsi_pre_tasks[level]; fptr < nsi_pre_tasks[level+1];
		fptr++) {
		if (*fptr) { /* LCOV_EXCL_BR_LINE */
			(*fptr)();
		}
	}
}

#endif /* defined(__APPLE__) */
