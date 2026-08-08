/*
 * Copyright (c) 2026 zenbedded contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/timing/timing.h>

#include <zenbedded/control.h>
#include <zenbedded/hist.h>
#include <zenbedded/plant.h>

struct zb_joint_rt zb_rt[ZB_JOINT_COUNT];
struct zb_hist zb_loop_hist;

#define ZB_PERIOD_US CONFIG_ZENBEDDED_CONTROL_PERIOD_US
#define ZB_DT_SEC    ((float)ZB_PERIOD_US * 1e-6f)

/*
 * If the kernel tick is coarser than the control period, k_timer quantises the
 * period up to the next tick and the loop runs slower than configured without
 * anything failing. That is a very quiet way to invalidate every number the
 * demo puts on a slide, so make it a build error instead.
 */
BUILD_ASSERT((USEC_PER_SEC / CONFIG_SYS_CLOCK_TICKS_PER_SEC) <= ZB_PERIOD_US,
	     "CONFIG_SYS_CLOCK_TICKS_PER_SEC is too coarse to express "
	     "CONFIG_ZENBEDDED_CONTROL_PERIOD_US; raise the tick rate");

static K_THREAD_STACK_DEFINE(zb_control_stack, CONFIG_ZENBEDDED_CONTROL_STACK_SIZE);
static struct k_thread zb_control_thread;
static K_TIMER_DEFINE(zb_control_timer, NULL, NULL);

/*
 * The loop is woken by the kernel timer, never by k_sleep(). The difference is
 * not stylistic: k_sleep() schedules relative to when the thread got around to
 * calling it, so every microsecond of jitter is carried into the next period
 * and the loop drifts. A periodic k_timer fires on an absolute schedule.
 *
 * k_timer_status_sync() also hands back the number of expirations since it was
 * last read, which is the overrun count for free and from the kernel's own
 * bookkeeping rather than from our arithmetic about our own lateness.
 *
 * No logging, no printk, no shell, no allocation, no network call, and no
 * synchronisation object the comms thread can touch. The only thing crossing
 * the thread boundary is the wait-free setpoint channel.
 */
static void zb_control_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	uint64_t now_us = 0;

	timing_init();
	timing_start();

	timing_t prev = timing_counter_get();

	k_timer_start(&zb_control_timer, K_USEC(ZB_PERIOD_US), K_USEC(ZB_PERIOD_US));

	for (;;) {
		uint32_t fired = k_timer_status_sync(&zb_control_timer);

		timing_t now = timing_counter_get();
		uint64_t ns = timing_cycles_to_ns(timing_cycles_get(&prev, &now));

		prev = now;

		if (IS_ENABLED(CONFIG_ZENBEDDED_HIST)) {
			zb_hist_record(&zb_loop_hist, (uint32_t)(ns / 1000ULL));
			if (fired > 1U) {
				zb_hist_record_overrun(&zb_loop_hist, fired - 1U);
			}
		}

		/*
		 * Advance device time by the periods that actually elapsed, so
		 * that a missed deadline ages the command by the right amount
		 * instead of quietly slowing the failover clock down.
		 */
		now_us += (uint64_t)ZB_PERIOD_US * fired;

		for (uint8_t i = 0; i < ZB_JOINT_COUNT; i++) {
			zb_control_step(&zb_rt[i], i, now_us, ZB_DT_SEC);
		}
	}
}

void zb_control_start(void)
{
	for (uint8_t i = 0; i < ZB_JOINT_COUNT; i++) {
		zb_joint_rt_init(&zb_rt[i], i);
	}

	zb_hist_reset(&zb_loop_hist);
	zb_plant_init();

	k_thread_create(&zb_control_thread, zb_control_stack,
			K_THREAD_STACK_SIZEOF(zb_control_stack), zb_control_thread_fn, NULL, NULL,
			NULL, K_PRIO_COOP(CONFIG_ZENBEDDED_CONTROL_PRIO), 0, K_NO_WAIT);
	k_thread_name_set(&zb_control_thread, "zb_control");
}

#ifdef CONFIG_ZENBEDDED_CONTROL_AUTOSTART
static int zb_control_autostart(void)
{
	zb_control_start();

	return 0;
}

SYS_INIT(zb_control_autostart, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
#endif /* CONFIG_ZENBEDDED_CONTROL_AUTOSTART */
