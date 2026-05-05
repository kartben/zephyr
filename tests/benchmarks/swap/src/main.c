/* Copyright 2025 The ChromiumOS Authors
 * SPDX-License-Identifier: Apache-2.0
 */

/* This app implements a simple microbenchmark of z_swap(), carefully
 * tuned to measure only cooperative swapping performance and nothing
 * else.  Build it and run.  No special usage needed.
 *
 * Subtle sequencing, see comments below.  This runs without a timer
 * driver (and in fact disables ARM SysTick so it can use it
 * directly), controlling execution order via scheduler priority only.
 */

#include <math.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>
#include <kswap.h>

/* Use a microtuned platform timing hook if available, otherwise k_cycle_get_32(). */
#ifdef CONFIG_CPU_CORTEX_M
#include "time_arm_m.h"
#else
#include "time_generic.h"
#endif

/* Check config for obvious mistakes. */
#ifdef CONFIG_ASSERT
#warning "This is a performance benchmark, debug features should not normally be enabled"
#define ORDER_CHECK() printk("%s:%d\n", __func__, __LINE__)
#else
#define ORDER_CHECK()
#endif

/* This ends up measuring a bunch of stuff not involved in arch code */
#if defined(CONFIG_TIMESLICING)
#warning "Timeslicing can pollute the microbenchmark"
#endif

/* And these absolutely explode the numbers, making the arch layer just noise */
#if defined(CONFIG_MPU) || defined(CONFIG_MMU)
#error "Don't enable memory management hardware in a microbenchmark!"
#endif

#if defined(CONFIG_FPU)
#error "Don't enable FPU/DSP in a microbenchmark!"
#endif

#if defined(CONFIG_HW_STACK_PROTECTION)
#error "Don't enable hardware stack protection in a microbenchmark!"
#endif

#define HI_PRIO   0
#define LO_PRIO   1
#define MAIN_PRIO 2
#define DONE_PRIO 3

void thread_fn(void *t0_arg, void *t1_arg, void *c);

/* swap enter/exit times for each thread */
int t0_0, t0_1, t1_0, t1_1;

struct k_spinlock lock;

K_SEM_DEFINE(done_sem, 0, 999);
K_THREAD_DEFINE(thread0, 1024, thread_fn, &t0_0, &t0_1, NULL, HI_PRIO, 0, 0);
K_THREAD_DEFINE(thread1, 1024, thread_fn, &t1_0, &t1_1, NULL, HI_PRIO, 0, 0);

#ifdef CONFIG_CPU_CORTEX_M
static uint32_t systick_ctrl;
static uint32_t systick_load;
#endif

void thread_fn(void *t0_arg, void *t1_arg, void *c)
{
	uint32_t t0, t1, *t0_out = t0_arg, *t1_out = t1_arg;

	while (true) {
		k_spinlock_key_t k = k_spin_lock(&lock);

		k_thread_priority_set(k_current_get(), DONE_PRIO);

		ORDER_CHECK();
		t0 = time();
		z_swap(&lock, k);
		t1 = time();

		*t0_out = t0;
		*t1_out = t1;
	}
}

struct k_timer tm;

K_SEM_DEFINE(hi_sem, 0, 9999);
uint32_t t_preempt;

void hi_thread_fn(void *a, void *b, void *c)
{
	while (true) {
		k_sem_take(&hi_sem, K_FOREVER);
		t_preempt = k_cycle_get_32();
	}
}

K_THREAD_DEFINE(hi_thread, 1024, hi_thread_fn, NULL, NULL, NULL, -1, 0, 0);

void timer0_fn(struct k_timer *t)
{
	/* empty */
}

static void irq_suite_setup(void)
{
	k_timer_init(&tm, timer0_fn, NULL);
}

ZTEST_BENCHMARK_SUITE(irq_benchmark, irq_suite_setup, NULL);

ZTEST_BENCHMARK(irq_benchmark, interrupt_latency, 1000)
{
	uint32_t t0, t1;
	unsigned int key;

	/* Lock interrupts before starting the timer, then wait for two ticks to
	 * roll over to be sure the interrupt is queued.
	 */
	key = arch_irq_lock();
	k_timer_start(&tm, K_TICKS(0), K_FOREVER);

	k_busy_wait(k_ticks_to_us_ceil32(3));

	/* Releasing the lock will fire the interrupt synchronously. */
	t0 = k_cycle_get_32();
	arch_irq_unlock(key);
	t1 = k_cycle_get_32();

	k_timer_stop(&tm);

	(void)(t1 - t0);
}

/* Very similar test, but switches to hi_thread and checks time on
 * interrupt exit there, to measure preemption overhead
 */
ZTEST_BENCHMARK(irq_benchmark, interrupt_preempt_latency, 1000)
{
	uint32_t t0;
	unsigned int key;

	key = arch_irq_lock();
	k_timer_start(&tm, K_TICKS(0), K_FOREVER);

	k_busy_wait(k_ticks_to_us_ceil32(3));

	k_sem_give(&hi_sem);
	t0 = k_cycle_get_32();
	arch_irq_unlock(key);

	k_timer_stop(&tm);

	(void)(t_preempt - t0);
}

static void swap_suite_setup(void)
{
#ifdef CONFIG_SMP
	zassert_equal(arch_num_cpus(), 1, "benchmark requires a single active CPU");
#endif

#ifdef CONFIG_CPU_CORTEX_M
	systick_ctrl = SysTick->CTRL;
	systick_load = SysTick->LOAD;
#endif
	time_setup();
	k_thread_priority_set(k_current_get(), MAIN_PRIO);
}

static void swap_suite_teardown(void)
{
	k_thread_abort(thread0);
	k_thread_abort(thread1);

#ifdef CONFIG_CPU_CORTEX_M
	SysTick->LOAD = systick_load;
	SysTick->CTRL = systick_ctrl;
#endif
}

ZTEST_BENCHMARK_SUITE(swap_benchmark, swap_suite_setup, swap_suite_teardown);

ZTEST_BENCHMARK(swap_benchmark, cooperative_swap, 1024)
{
	/* The threads are launched by the kernel at HI priority, so
	 * they've already run and are suspended in swap for us.
	 */
	k_sched_lock();

	ORDER_CHECK();
	k_thread_priority_set(thread0, HI_PRIO);
	ORDER_CHECK();
	k_thread_priority_set(thread1, LO_PRIO);
	ORDER_CHECK();

	/* Now unlock: thread0 will run first, looping around and calling z_swap,
	 * with an entry timestamp of t0_0. That will swap to thread1 (which
	 * timestamps its exit as t1_1). Then we end up back here.
	 */
	ORDER_CHECK();
	k_sched_unlock();

	/* thread1 "woke up" on our cycle and stored its exit time in t1_1. But
	 * thread0's entry time is still a local variable suspended on its stack,
	 * so pump it once to get it to store its output.
	 */
	k_thread_priority_set(thread0, HI_PRIO);

	(void)time_delta(t0_0, t1_1);
}
