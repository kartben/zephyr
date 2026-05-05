/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <wait_q.h>
#include <ksched.h>

/* This is a scheduler microbenchmark, designed to measure latencies
 * of specific low level scheduling primitives independent of overhead
 * from application or API abstractions.  It works very simply: a main
 * thread creates a "partner" thread at a higher priority, the partner
 * then sleeps using z_pend_curr().  From this initial
 * state:
 *
 * 1. The main thread calls z_unpend_first_thread()
 * 2. The main thread calls z_ready_thread()
 * 3. The main thread calls k_yield()
 *    (the kernel switches to the partner thread)
 * 4. The partner thread then runs and calls z_pend_curr() again
 *    (the kernel switches to the main thread)
 * 5. The main thread returns from k_yield()
 *
 * It then iterates this many times, reporting timestamp latencies
 * between each numbered step and for the whole cycle, and a running
 * average for all cycles run.
 */

static K_THREAD_STACK_DEFINE(partner_stack, 1024);
static struct k_thread partner_thread;
static k_tid_t partner_tid;

#if (CONFIG_MP_MAX_NUM_CPUS > 1)
static struct k_thread busy_thread[CONFIG_MP_MAX_NUM_CPUS - 1];

#define BUSY_THREAD_STACK_SIZE  (1024 + CONFIG_TEST_EXTRA_STACK_SIZE)

static K_THREAD_STACK_ARRAY_DEFINE(busy_thread_stack, CONFIG_MP_MAX_NUM_CPUS - 1,
				   BUSY_THREAD_STACK_SIZE);
#endif /* (CONFIG_MP_MAX_NUM_CPUS > 1) */

_wait_q_t waitq;

static struct k_spinlock lock;

static void partner_fn(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_spinlock_key_t  key = k_spin_lock(&lock);

		z_pend_curr(&lock, key, &waitq, K_FOREVER);
	}
}

#if (CONFIG_MP_MAX_NUM_CPUS > 1)
static void busy_thread_entry(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
	}
}
#endif /* (CONFIG_MP_MAX_NUM_CPUS > 1) */

static void sched_benchmark_suite_setup(void)
{
#if (CONFIG_MP_MAX_NUM_CPUS > 1)
	/* Spawn busy threads that will execute on the other cores */
	for (uint32_t i = 0; i < CONFIG_MP_MAX_NUM_CPUS - 1; i++) {
		k_thread_create(&busy_thread[i], busy_thread_stack[i],
				BUSY_THREAD_STACK_SIZE, busy_thread_entry,
				NULL, NULL, NULL,
				K_HIGHEST_THREAD_PRIO, 0, K_NO_WAIT);
	}
#endif /* (CONFIG_MP_MAX_NUM_CPUS > 1) */

	z_waitq_init(&waitq);

	const int main_prio = k_thread_priority_get(k_current_get());
	const int partner_prio = main_prio - 1;

	partner_tid = k_thread_create(&partner_thread, partner_stack,
				      K_THREAD_STACK_SIZEOF(partner_stack),
				      partner_fn, NULL, NULL, NULL,
				      partner_prio, 0, K_NO_WAIT);

	/* Let it start running and pend */
	k_sleep(K_MSEC(100));
}

static void sched_benchmark_suite_teardown(void)
{
	k_thread_abort(partner_tid);

#if (CONFIG_MP_MAX_NUM_CPUS > 1)
	for (uint32_t i = 0; i < CONFIG_MP_MAX_NUM_CPUS - 1; i++) {
		k_thread_abort(&busy_thread[i]);
	}
#endif /* (CONFIG_MP_MAX_NUM_CPUS > 1) */
}

ZTEST_BENCHMARK_SUITE(sched_benchmark, sched_benchmark_suite_setup,
		      sched_benchmark_suite_teardown);

ZTEST_BENCHMARK(sched_benchmark, scheduler_cycle, 1000)
{
	const unsigned int key = arch_irq_lock();

	z_unpend_first_thread(&waitq);
	arch_irq_unlock(key);
	z_ready_thread(partner_tid);

	/* z_ready_thread() does not reschedule, so this is guaranteed to be the
	 * point where we will yield to the new thread, which (being higher
	 * priority) will run immediately, and we'll wake up synchronously as soon
	 * as it pends.
	 */
	k_yield();
}
