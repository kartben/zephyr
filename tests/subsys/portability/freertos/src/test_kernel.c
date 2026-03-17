/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <FreeRTOS.h>
#include <task.h>

/* --- Tick Count Tests --- */

ZTEST(freertos_kernel, test_tick_count_increases)
{
	TickType_t t1 = xTaskGetTickCount();

	k_msleep(50);

	TickType_t t2 = xTaskGetTickCount();

	zassert_true(t2 > t1, "Tick count did not increase");
}

ZTEST(freertos_kernel, test_tick_count_nonzero)
{
	/* Tick count should be nonzero after boot */
	TickType_t t = xTaskGetTickCount();

	zassert_true(t > 0, "Tick count should be nonzero");
}

/* --- Scheduler Start (No-op) --- */

ZTEST(freertos_kernel, test_start_scheduler_noop)
{
	/* Should not crash or block */
	vTaskStartScheduler();
}

/* --- Constants --- */

ZTEST(freertos_kernel, test_constants)
{
	zassert_equal(pdTRUE, 1);
	zassert_equal(pdFALSE, 0);
	zassert_equal(pdPASS, 1);
	zassert_equal(pdFAIL, 0);
	zassert_equal(portMAX_DELAY, UINT32_MAX);
}

/* --- Config Macros --- */

ZTEST(freertos_kernel, test_config_macros)
{
	zassert_equal(configMAX_PRIORITIES, CONFIG_FREERTOS_MAX_PRIORITIES);
	zassert_equal(configMINIMAL_STACK_SIZE,
		      CONFIG_FREERTOS_TASK_DEFAULT_STACK_SIZE);
	zassert_equal(configTICK_RATE_HZ, CONFIG_SYS_CLOCK_TICKS_PER_SEC);
}

/* --- pdMS_TO_TICKS --- */

ZTEST(freertos_kernel, test_ms_to_ticks)
{
	TickType_t ticks = pdMS_TO_TICKS(1000);

	/* Should equal configTICK_RATE_HZ for 1000ms */
	zassert_true(ticks > 0, "pdMS_TO_TICKS(1000) should be > 0");
}

/* --- Critical Sections --- */

ZTEST(freertos_kernel, test_critical_section)
{
	volatile int val = 0;

	taskENTER_CRITICAL();
	val = 42;
	taskEXIT_CRITICAL();

	zassert_equal(val, 42, "Value not set in critical section");
}

/* --- taskYIELD --- */

ZTEST(freertos_kernel, test_yield)
{
	/* Should not crash or block */
	taskYIELD();
}

ZTEST_SUITE(freertos_kernel, NULL, NULL, NULL, NULL, NULL);
