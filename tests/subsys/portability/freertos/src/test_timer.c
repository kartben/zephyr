/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <FreeRTOS.h>
#include <task.h>
#include <timers.h>
#include <semphr.h>

static volatile int timer_callback_count;

static void test_timer_cb(TimerHandle_t xTimer)
{
	timer_callback_count++;
}

/* --- Basic Timer Tests --- */

ZTEST(freertos_timer, test_timer_create)
{
	TimerHandle_t timer = xTimerCreate("TestTimer", pdMS_TO_TICKS(100),
					   pdTRUE, NULL, test_timer_cb);

	zassert_not_null(timer, "Timer creation failed");

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_create_null_callback)
{
	TimerHandle_t timer = xTimerCreate("BadTimer", pdMS_TO_TICKS(100),
					   pdTRUE, NULL, NULL);

	zassert_is_null(timer, "NULL callback should fail");
}

ZTEST(freertos_timer, test_timer_periodic)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("Periodic", pdMS_TO_TICKS(50),
					   pdTRUE, NULL, test_timer_cb);

	zassert_not_null(timer);

	zassert_equal(xTimerStart(timer, 0), pdPASS);

	/* Wait for ~250ms - should get ~5 callbacks */
	k_msleep(275);

	xTimerStop(timer, 0);

	int count = timer_callback_count;

	zassert_true(count >= 4, "Too few callbacks: %d", count);
	zassert_true(count <= 7, "Too many callbacks: %d", count);

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_one_shot)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("OneShot", pdMS_TO_TICKS(50),
					   pdFALSE, NULL, test_timer_cb);

	zassert_not_null(timer);

	zassert_equal(xTimerStart(timer, 0), pdPASS);

	k_msleep(200);

	/* One-shot should fire exactly once */
	zassert_equal(timer_callback_count, 1,
		      "One-shot fired %d times", timer_callback_count);

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_stop)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("StopTest", pdMS_TO_TICKS(50),
					   pdTRUE, NULL, test_timer_cb);

	zassert_not_null(timer);

	xTimerStart(timer, 0);
	k_msleep(75);

	zassert_equal(xTimerStop(timer, 0), pdPASS);

	int count_at_stop = timer_callback_count;

	k_msleep(150);

	/* No more callbacks after stop */
	zassert_equal(timer_callback_count, count_at_stop,
		      "Timer continued after stop");

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_reset)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("ResetTest", pdMS_TO_TICKS(100),
					   pdFALSE, NULL, test_timer_cb);

	zassert_not_null(timer);

	xTimerStart(timer, 0);

	/* Reset before it fires */
	k_msleep(50);
	xTimerReset(timer, 0);

	/* After reset, should fire ~100ms from now */
	k_msleep(50);
	zassert_equal(timer_callback_count, 0, "Timer fired too early after reset");

	k_msleep(75);
	zassert_equal(timer_callback_count, 1, "Timer should have fired after reset period");

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_change_period)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("ChangePeriod", pdMS_TO_TICKS(200),
					   pdTRUE, NULL, test_timer_cb);

	zassert_not_null(timer);

	xTimerStart(timer, 0);

	/* Change to shorter period */
	xTimerChangePeriod(timer, pdMS_TO_TICKS(50), 0);

	k_msleep(275);
	xTimerStop(timer, 0);

	/* With 50ms period over 275ms, expect ~5 callbacks */
	zassert_true(timer_callback_count >= 4,
		     "Too few after period change: %d", timer_callback_count);

	xTimerDelete(timer, 0);
}

/* --- Timer ID Tests --- */

ZTEST(freertos_timer, test_timer_id)
{
	int my_data = 42;

	TimerHandle_t timer = xTimerCreate("IDTest", pdMS_TO_TICKS(100),
					   pdFALSE, &my_data, test_timer_cb);

	zassert_not_null(timer);

	void *id = pvTimerGetTimerID(timer);

	zassert_equal(id, &my_data, "Timer ID mismatch");
	zassert_equal(*(int *)id, 42, "Timer ID data mismatch");

	xTimerDelete(timer, 0);
}

ZTEST(freertos_timer, test_timer_id_null)
{
	zassert_is_null(pvTimerGetTimerID(NULL));
}

/* --- Timer Callback Receives Handle --- */

static TimerHandle_t cb_received_handle;

static void handle_check_cb(TimerHandle_t xTimer)
{
	cb_received_handle = xTimer;
}

ZTEST(freertos_timer, test_timer_callback_receives_handle)
{
	cb_received_handle = NULL;

	TimerHandle_t timer = xTimerCreate("HandleCB", pdMS_TO_TICKS(50),
					   pdFALSE, NULL, handle_check_cb);

	zassert_not_null(timer);

	xTimerStart(timer, 0);
	k_msleep(100);

	zassert_equal(cb_received_handle, timer,
		      "Callback did not receive correct handle");

	xTimerDelete(timer, 0);
}

/* --- NULL Handle Tests --- */

ZTEST(freertos_timer, test_timer_null_handle)
{
	zassert_equal(xTimerStart(NULL, 0), pdFAIL);
	zassert_equal(xTimerStop(NULL, 0), pdFAIL);
	zassert_equal(xTimerDelete(NULL, 0), pdFAIL);
	zassert_equal(xTimerChangePeriod(NULL, 100, 0), pdFAIL);
	zassert_equal(xTimerReset(NULL, 0), pdFAIL);
}

/* --- Delete Active Timer --- */

ZTEST(freertos_timer, test_timer_delete_while_active)
{
	timer_callback_count = 0;

	TimerHandle_t timer = xTimerCreate("DelActive", pdMS_TO_TICKS(50),
					   pdTRUE, NULL, test_timer_cb);

	zassert_not_null(timer);

	xTimerStart(timer, 0);
	k_msleep(75);

	/* Delete while running - should stop and free */
	zassert_equal(xTimerDelete(timer, 0), pdPASS);

	int count_at_delete = timer_callback_count;

	k_msleep(150);
	zassert_equal(timer_callback_count, count_at_delete,
		      "Timer continued after delete");
}

/* --- Max Allocation --- */

ZTEST(freertos_timer, test_timer_max_allocation)
{
	TimerHandle_t timers[CONFIG_FREERTOS_TIMER_MAX_COUNT + 1];
	int i;

	for (i = 0; i <= CONFIG_FREERTOS_TIMER_MAX_COUNT; i++) {
		timers[i] = xTimerCreate("MaxTest", pdMS_TO_TICKS(1000),
					 pdFALSE, NULL, test_timer_cb);
		if (i == CONFIG_FREERTOS_TIMER_MAX_COUNT) {
			zassert_is_null(timers[i],
					"Should fail after max count");
		} else {
			zassert_not_null(timers[i],
					 "Allocation %d should succeed", i);
		}
	}

	for (i = 0; i < CONFIG_FREERTOS_TIMER_MAX_COUNT; i++) {
		xTimerDelete(timers[i], 0);
	}
}

ZTEST_SUITE(freertos_timer, NULL, NULL, NULL, NULL, NULL);
