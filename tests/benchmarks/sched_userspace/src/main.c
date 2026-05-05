/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

/* private kernel APIs */
#include <wait_q.h>
#include <ksched.h>

#include "app_threads.h"
#include "user.h"

#define MAIN_PRIO 8
#define THREADS_PRIO 9
#define SCHED_USERSPACE_SAMPLES 5

static int yielder_status;

void yielder_entry(void *_thread, void *_tid, void *_nb_threads)
{
	struct k_app_thread *thread = (struct k_app_thread *) _thread;
	int ret;

	struct k_mem_partition *parts[] = {
		thread->partition,
	};

	ret = k_mem_domain_init(&thread->domain, ARRAY_SIZE(parts), parts);
	if (ret != 0) {
		printk("k_mem_domain_init failed %d\n", ret);
		yielder_status = 1;
		return;
	}

	k_mem_domain_add_thread(&thread->domain, k_current_get());

	k_thread_user_mode_enter(context_switch_yield, _nb_threads, NULL, NULL);
}


static k_tid_t threads[MAX_NB_THREADS];

static int exec_test(uint8_t nb_threads)
{
	if (nb_threads > MAX_NB_THREADS) {
		return -1;
	}

	yielder_status = 0;

	for (size_t tid = 0; tid < nb_threads; tid++) {
		app_threads[tid].partition = app_partitions[tid];
		app_threads[tid].stack = &app_thread_stacks[tid];

		void *_tid = (void *)(uintptr_t)tid;

		threads[tid] = k_thread_create(&app_threads[tid].thread,
					app_thread_stacks[tid],
					APP_STACKSIZE, yielder_entry,
					&app_threads[tid], _tid, (void *)(uintptr_t)nb_threads,
					THREADS_PRIO, 0, K_FOREVER);
	}

	/* make sure the main thread has a higher priority
	 * this way, user threads all start together
	 * (lower number --> higher prio)
	 */
	k_thread_priority_set(k_current_get(), MAIN_PRIO);

	for (size_t tid = 0; tid < nb_threads; tid++) {
		k_thread_start(threads[tid]);
	}
	for (size_t tid = 0; tid < nb_threads; tid++) {
		k_thread_join(threads[tid], K_FOREVER);
	}

	return yielder_status;
}

ZTEST_BENCHMARK_SUITE(sched_userspace_benchmark, NULL, NULL);

ZTEST_BENCHMARK(sched_userspace_benchmark, swap_yield_2_threads, SCHED_USERSPACE_SAMPLES)
{
	zassert_equal(exec_test(2), 0, "2-thread userspace benchmark failed");
}

ZTEST_BENCHMARK(sched_userspace_benchmark, swap_yield_8_threads, SCHED_USERSPACE_SAMPLES)
{
	zassert_equal(exec_test(8), 0, "8-thread userspace benchmark failed");
}

ZTEST_BENCHMARK(sched_userspace_benchmark, swap_yield_16_threads, SCHED_USERSPACE_SAMPLES)
{
	zassert_equal(exec_test(16), 0, "16-thread userspace benchmark failed");
}

ZTEST_BENCHMARK(sched_userspace_benchmark, swap_yield_32_threads, SCHED_USERSPACE_SAMPLES)
{
	zassert_equal(exec_test(32), 0, "32-thread userspace benchmark failed");
}
