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

/* --- Binary Semaphore Tests --- */

ZTEST(freertos_semaphore, test_binary_semaphore_create)
{
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();

	zassert_not_null(sem, "Binary semaphore creation failed");

	/* Binary semaphore starts at count 0 */
	BaseType_t ret = xSemaphoreTake(sem, 0);

	zassert_equal(ret, pdFALSE, "Binary sem should start empty");

	vSemaphoreDelete(sem);
}

ZTEST(freertos_semaphore, test_binary_semaphore_give_take)
{
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();

	zassert_not_null(sem);

	/* Give then take */
	zassert_equal(xSemaphoreGive(sem), pdTRUE, "Give failed");
	zassert_equal(xSemaphoreTake(sem, 0), pdTRUE, "Take failed after give");

	/* Take again should fail (empty) */
	zassert_equal(xSemaphoreTake(sem, 0), pdFALSE, "Take should fail when empty");

	vSemaphoreDelete(sem);
}

ZTEST(freertos_semaphore, test_binary_semaphore_max_count)
{
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();

	zassert_not_null(sem);

	/* Give twice - binary sem max is 1, second give should still succeed
	 * (k_sem_give is idempotent at max) but sem count stays at 1
	 */
	xSemaphoreGive(sem);
	xSemaphoreGive(sem);

	/* Should be able to take once */
	zassert_equal(xSemaphoreTake(sem, 0), pdTRUE);
	/* Second take should fail */
	zassert_equal(xSemaphoreTake(sem, 0), pdFALSE);

	vSemaphoreDelete(sem);
}

/* --- Counting Semaphore Tests --- */

ZTEST(freertos_semaphore, test_counting_semaphore_create)
{
	SemaphoreHandle_t sem = xSemaphoreCreateCounting(5, 3);

	zassert_not_null(sem, "Counting semaphore creation failed");

	/* Should be able to take 3 times (initial count) */
	for (int i = 0; i < 3; i++) {
		zassert_equal(xSemaphoreTake(sem, 0), pdTRUE,
			      "Take %d should succeed", i);
	}
	/* 4th take should fail */
	zassert_equal(xSemaphoreTake(sem, 0), pdFALSE,
		      "Take should fail when empty");

	vSemaphoreDelete(sem);
}

ZTEST(freertos_semaphore, test_counting_semaphore_max)
{
	SemaphoreHandle_t sem = xSemaphoreCreateCounting(3, 0);

	zassert_not_null(sem);

	/* Give 3 times (up to max) */
	for (int i = 0; i < 3; i++) {
		xSemaphoreGive(sem);
	}

	/* Take 3 times */
	for (int i = 0; i < 3; i++) {
		zassert_equal(xSemaphoreTake(sem, 0), pdTRUE);
	}
	/* Next take should fail */
	zassert_equal(xSemaphoreTake(sem, 0), pdFALSE);

	vSemaphoreDelete(sem);
}

/* --- Mutex Tests --- */

ZTEST(freertos_semaphore, test_mutex_create)
{
	SemaphoreHandle_t mtx = xSemaphoreCreateMutex();

	zassert_not_null(mtx, "Mutex creation failed");

	/* Mutex starts in "given" state, so first take should succeed */
	zassert_equal(xSemaphoreTake(mtx, 0), pdTRUE, "Mutex take failed");

	/* Release it */
	zassert_equal(xSemaphoreGive(mtx), pdTRUE, "Mutex give failed");

	vSemaphoreDelete(mtx);
}

ZTEST(freertos_semaphore, test_mutex_recursive_take)
{
	SemaphoreHandle_t mtx = xSemaphoreCreateMutex();

	zassert_not_null(mtx);

	/* Zephyr k_mutex supports recursive locking */
	zassert_equal(xSemaphoreTake(mtx, 0), pdTRUE);
	zassert_equal(xSemaphoreTake(mtx, 0), pdTRUE);

	/* Must give twice */
	zassert_equal(xSemaphoreGive(mtx), pdTRUE);
	zassert_equal(xSemaphoreGive(mtx), pdTRUE);

	vSemaphoreDelete(mtx);
}

/* --- Timeout Tests --- */

ZTEST(freertos_semaphore, test_semaphore_take_timeout)
{
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();

	zassert_not_null(sem);

	TickType_t start = xTaskGetTickCount();
	BaseType_t ret = xSemaphoreTake(sem, pdMS_TO_TICKS(100));
	TickType_t elapsed = xTaskGetTickCount() - start;

	zassert_equal(ret, pdFALSE, "Should have timed out");
	zassert_true(elapsed >= pdMS_TO_TICKS(80),
		     "Timeout too short: %u ticks", elapsed);

	vSemaphoreDelete(sem);
}

ZTEST(freertos_semaphore, test_semaphore_take_forever)
{
	SemaphoreHandle_t sem = xSemaphoreCreateBinary();

	zassert_not_null(sem);

	/* Give first so the blocking take succeeds */
	xSemaphoreGive(sem);

	BaseType_t ret = xSemaphoreTake(sem, portMAX_DELAY);

	zassert_equal(ret, pdTRUE, "portMAX_DELAY take should succeed");

	vSemaphoreDelete(sem);
}

/* --- NULL Handle Tests --- */

ZTEST(freertos_semaphore, test_semaphore_null_handle)
{
	zassert_equal(xSemaphoreTake(NULL, 0), pdFALSE);
	zassert_equal(xSemaphoreGive(NULL), pdFALSE);
	/* vSemaphoreDelete(NULL) should not crash */
	vSemaphoreDelete(NULL);
}

/* --- Multi-task Semaphore Synchronization --- */

static SemaphoreHandle_t sync_sem;
static volatile int sync_value;

static void sem_producer(void *pvParameters)
{
	for (int i = 0; i < 5; i++) {
		sync_value = i;
		xSemaphoreGive(sync_sem);
		vTaskDelay(pdMS_TO_TICKS(20));
	}
	vTaskDelete(NULL);
}

static SemaphoreHandle_t test_complete_sem;

static void sem_consumer(void *pvParameters)
{
	for (int i = 0; i < 5; i++) {
		xSemaphoreTake(sync_sem, portMAX_DELAY);
		zassert_equal(sync_value, i, "Sync value mismatch at %d", i);
	}
	xSemaphoreGive(test_complete_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_semaphore, test_semaphore_multitask_sync)
{
	sync_sem = xSemaphoreCreateBinary();
	test_complete_sem = xSemaphoreCreateBinary();
	zassert_not_null(sync_sem);
	zassert_not_null(test_complete_sem);

	/* Consumer at higher priority waits on semaphore */
	xTaskCreate(sem_consumer, "Consumer", configMINIMAL_STACK_SIZE,
		    NULL, 3, NULL);
	xTaskCreate(sem_producer, "Producer", configMINIMAL_STACK_SIZE,
		    NULL, 2, NULL);

	xSemaphoreTake(test_complete_sem, portMAX_DELAY);

	vSemaphoreDelete(sync_sem);
	vSemaphoreDelete(test_complete_sem);
}

/* --- Max Allocation Tests --- */

ZTEST(freertos_semaphore, test_semaphore_max_allocation)
{
	SemaphoreHandle_t sems[CONFIG_FREERTOS_SEMAPHORE_MAX_COUNT + 1];
	int i;

	for (i = 0; i <= CONFIG_FREERTOS_SEMAPHORE_MAX_COUNT; i++) {
		sems[i] = xSemaphoreCreateBinary();
		if (i == CONFIG_FREERTOS_SEMAPHORE_MAX_COUNT) {
			zassert_is_null(sems[i],
					"Should fail after max count");
		} else {
			zassert_not_null(sems[i],
					 "Allocation %d should succeed", i);
		}
	}

	/* Cleanup */
	for (i = 0; i < CONFIG_FREERTOS_SEMAPHORE_MAX_COUNT; i++) {
		vSemaphoreDelete(sems[i]);
	}
}

ZTEST_SUITE(freertos_semaphore, NULL, NULL, NULL, NULL, NULL);
