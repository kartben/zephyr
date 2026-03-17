/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

static volatile int task_ran;
static SemaphoreHandle_t task_done_sem;

static void simple_task(void *pvParameters)
{
	task_ran = (int)(intptr_t)pvParameters;
	xSemaphoreGive(task_done_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_task, test_task_create_delete)
{
	TaskHandle_t handle = NULL;
	BaseType_t ret;

	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem, "Failed to create semaphore");

	task_ran = 0;
	ret = xTaskCreate(simple_task, "TestTask", configMINIMAL_STACK_SIZE,
			  (void *)42, 2, &handle);
	zassert_equal(ret, pdPASS, "xTaskCreate failed");
	zassert_not_null(handle, "Task handle should be non-NULL");

	/* Wait for task to complete */
	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	zassert_equal(task_ran, 42, "Task parameter not passed correctly");

	vSemaphoreDelete(task_done_sem);
}

ZTEST(freertos_task, test_task_create_null_handle)
{
	BaseType_t ret;

	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem, "Failed to create semaphore");

	task_ran = 0;
	ret = xTaskCreate(simple_task, "NoHandle", configMINIMAL_STACK_SIZE,
			  (void *)99, 1, NULL);
	zassert_equal(ret, pdPASS, "xTaskCreate with NULL handle should succeed");

	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	zassert_equal(task_ran, 99, "Task did not run");

	vSemaphoreDelete(task_done_sem);
}

ZTEST(freertos_task, test_task_create_null_function)
{
	BaseType_t ret;

	ret = xTaskCreate(NULL, "BadTask", configMINIMAL_STACK_SIZE,
			  NULL, 1, NULL);
	zassert_equal(ret, pdFAIL, "NULL task function should fail");
}

static volatile int delay_task_done;

static void delay_task(void *pvParameters)
{
	TickType_t start = xTaskGetTickCount();

	vTaskDelay(pdMS_TO_TICKS(100));

	TickType_t elapsed = xTaskGetTickCount() - start;

	/* Should have delayed at least ~100ms worth of ticks */
	zassert_true(elapsed >= pdMS_TO_TICKS(80),
		     "vTaskDelay too short: %u ticks", elapsed);

	delay_task_done = 1;
	xSemaphoreGive(task_done_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_task, test_task_delay)
{
	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem);

	delay_task_done = 0;
	xTaskCreate(delay_task, "DelayTask", configMINIMAL_STACK_SIZE,
		    NULL, 2, NULL);

	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	zassert_equal(delay_task_done, 1, "Delay task did not complete");

	vSemaphoreDelete(task_done_sem);
}

ZTEST(freertos_task, test_task_delay_zero_yields)
{
	/* vTaskDelay(0) should yield, not block */
	vTaskDelay(0);
	/* If we get here, it worked (didn't hang) */
}

static void delay_until_task(void *pvParameters)
{
	TickType_t last_wake = xTaskGetTickCount();
	int i;

	for (i = 0; i < 3; i++) {
		xTaskDelayUntil(&last_wake, pdMS_TO_TICKS(50));
	}

	xSemaphoreGive(task_done_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_task, test_task_delay_until)
{
	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem);

	TickType_t start = xTaskGetTickCount();

	xTaskCreate(delay_until_task, "DelayUntil", configMINIMAL_STACK_SIZE,
		    NULL, 2, NULL);

	xSemaphoreTake(task_done_sem, portMAX_DELAY);

	TickType_t elapsed = xTaskGetTickCount() - start;

	/* 3 iterations * 50ms = ~150ms */
	zassert_true(elapsed >= pdMS_TO_TICKS(120),
		     "DelayUntil total too short: %u", elapsed);

	vSemaphoreDelete(task_done_sem);
}

static void priority_task(void *pvParameters)
{
	UBaseType_t prio = uxTaskPriorityGet(NULL);

	zassert_equal(prio, 3, "Initial priority wrong: %u", prio);

	vTaskPrioritySet(NULL, 5);
	prio = uxTaskPriorityGet(NULL);
	zassert_equal(prio, 5, "Priority after set wrong: %u", prio);

	xSemaphoreGive(task_done_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_task, test_task_priority)
{
	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem);

	xTaskCreate(priority_task, "PrioTask", configMINIMAL_STACK_SIZE,
		    NULL, 3, NULL);

	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	vSemaphoreDelete(task_done_sem);
}

ZTEST(freertos_task, test_task_priority_get_set_by_handle)
{
	TaskHandle_t handle;

	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem);

	xTaskCreate(simple_task, "HandlePrio", configMINIMAL_STACK_SIZE,
		    (void *)1, 4, &handle);

	UBaseType_t prio = uxTaskPriorityGet(handle);

	zassert_equal(prio, 4, "Priority via handle wrong: %u", prio);

	vTaskPrioritySet(handle, 2);
	prio = uxTaskPriorityGet(handle);
	zassert_equal(prio, 2, "Priority after set via handle wrong: %u", prio);

	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	vSemaphoreDelete(task_done_sem);
}

ZTEST(freertos_task, test_task_get_tick_count)
{
	TickType_t t1 = xTaskGetTickCount();

	k_msleep(50);

	TickType_t t2 = xTaskGetTickCount();

	zassert_true(t2 > t1, "Tick count should increase over time");
}

ZTEST(freertos_task, test_task_start_scheduler_noop)
{
	/* Should not crash or block */
	vTaskStartScheduler();
}

static void self_deleting_task(void *pvParameters)
{
	xSemaphoreGive(task_done_sem);
	vTaskDelete(NULL);
	/* Should never reach here */
}

ZTEST(freertos_task, test_task_delete_self)
{
	task_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(task_done_sem);

	xTaskCreate(self_deleting_task, "SelfDel", configMINIMAL_STACK_SIZE,
		    NULL, 2, NULL);

	xSemaphoreTake(task_done_sem, portMAX_DELAY);
	/* Small delay to let the task actually delete itself */
	k_msleep(50);

	vSemaphoreDelete(task_done_sem);
}

static void long_running_task(void *pvParameters)
{
	while (1) {
		vTaskDelay(pdMS_TO_TICKS(1000));
	}
}

ZTEST(freertos_task, test_task_delete_by_handle)
{
	TaskHandle_t handle;

	BaseType_t ret = xTaskCreate(long_running_task, "LongTask",
				     configMINIMAL_STACK_SIZE, NULL, 1, &handle);
	zassert_equal(ret, pdPASS);

	k_msleep(10);
	vTaskDelete(handle);
	/* If we get here without hanging, deletion worked */
}

ZTEST_SUITE(freertos_task, NULL, NULL, NULL, NULL, NULL);
