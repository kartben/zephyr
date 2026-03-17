/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/kernel.h>
#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <semphr.h>

/* --- Basic Queue Tests --- */

ZTEST(freertos_queue, test_queue_create)
{
	QueueHandle_t q = xQueueCreate(5, sizeof(uint32_t));

	zassert_not_null(q, "Queue creation failed");
	zassert_equal(uxQueueMessagesWaiting(q), 0, "New queue should be empty");
	zassert_equal(uxQueueSpacesAvailable(q), 5, "New queue should have 5 spaces");

	vQueueDelete(q);
}

ZTEST(freertos_queue, test_queue_create_invalid)
{
	QueueHandle_t q;

	q = xQueueCreate(0, sizeof(uint32_t));
	zassert_is_null(q, "Zero-length queue should fail");

	q = xQueueCreate(5, 0);
	zassert_is_null(q, "Zero-size items should fail");
}

ZTEST(freertos_queue, test_queue_send_receive)
{
	QueueHandle_t q = xQueueCreate(3, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t send_val = 42;
	uint32_t recv_val = 0;

	zassert_equal(xQueueSend(q, &send_val, 0), pdTRUE,
		      "Send should succeed");
	zassert_equal(uxQueueMessagesWaiting(q), 1);

	zassert_equal(xQueueReceive(q, &recv_val, 0), pdTRUE,
		      "Receive should succeed");
	zassert_equal(recv_val, 42, "Received wrong value");
	zassert_equal(uxQueueMessagesWaiting(q), 0);

	vQueueDelete(q);
}

ZTEST(freertos_queue, test_queue_send_to_back)
{
	QueueHandle_t q = xQueueCreate(3, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t vals[] = {1, 2, 3};
	uint32_t recv;

	for (int i = 0; i < 3; i++) {
		zassert_equal(xQueueSendToBack(q, &vals[i], 0), pdTRUE);
	}

	/* Should receive in FIFO order */
	for (int i = 0; i < 3; i++) {
		zassert_equal(xQueueReceive(q, &recv, 0), pdTRUE);
		zassert_equal(recv, vals[i], "FIFO order broken at %d", i);
	}

	vQueueDelete(q);
}

ZTEST(freertos_queue, test_queue_full)
{
	QueueHandle_t q = xQueueCreate(2, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t val = 1;

	zassert_equal(xQueueSend(q, &val, 0), pdTRUE);
	val = 2;
	zassert_equal(xQueueSend(q, &val, 0), pdTRUE);

	/* Queue is full */
	zassert_equal(uxQueueSpacesAvailable(q), 0);
	val = 3;
	zassert_equal(xQueueSend(q, &val, 0), errQUEUE_FULL,
		      "Send to full queue should fail");

	vQueueDelete(q);
}

ZTEST(freertos_queue, test_queue_empty_receive)
{
	QueueHandle_t q = xQueueCreate(2, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t recv;

	zassert_equal(xQueueReceive(q, &recv, 0), pdFALSE,
		      "Receive from empty queue should fail");

	vQueueDelete(q);
}

/* --- Timeout Tests --- */

ZTEST(freertos_queue, test_queue_receive_timeout)
{
	QueueHandle_t q = xQueueCreate(2, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t recv;
	TickType_t start = xTaskGetTickCount();

	zassert_equal(xQueueReceive(q, &recv, pdMS_TO_TICKS(100)), pdFALSE);

	TickType_t elapsed = xTaskGetTickCount() - start;

	zassert_true(elapsed >= pdMS_TO_TICKS(80),
		     "Timeout too short: %u", elapsed);

	vQueueDelete(q);
}

ZTEST(freertos_queue, test_queue_send_timeout)
{
	QueueHandle_t q = xQueueCreate(1, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t val = 1;

	/* Fill the queue */
	xQueueSend(q, &val, 0);

	TickType_t start = xTaskGetTickCount();

	val = 2;
	zassert_equal(xQueueSend(q, &val, pdMS_TO_TICKS(100)), errQUEUE_FULL);

	TickType_t elapsed = xTaskGetTickCount() - start;

	zassert_true(elapsed >= pdMS_TO_TICKS(80),
		     "Send timeout too short: %u", elapsed);

	vQueueDelete(q);
}

/* --- Multi-item Type Tests --- */

struct test_msg {
	uint32_t id;
	uint8_t data[8];
};

ZTEST(freertos_queue, test_queue_struct_items)
{
	QueueHandle_t q = xQueueCreate(3, sizeof(struct test_msg));

	zassert_not_null(q);

	struct test_msg send = {.id = 7};

	memset(send.data, 0xAB, sizeof(send.data));

	zassert_equal(xQueueSend(q, &send, 0), pdTRUE);

	struct test_msg recv = {0};

	zassert_equal(xQueueReceive(q, &recv, 0), pdTRUE);
	zassert_equal(recv.id, 7);
	zassert_mem_equal(recv.data, send.data, sizeof(send.data));

	vQueueDelete(q);
}

/* --- NULL Handle Tests --- */

ZTEST(freertos_queue, test_queue_null_handle)
{
	uint32_t val = 0;

	zassert_equal(xQueueSend(NULL, &val, 0), errQUEUE_FULL);
	zassert_equal(xQueueReceive(NULL, &val, 0), pdFALSE);
	zassert_equal(uxQueueMessagesWaiting(NULL), 0);
	zassert_equal(uxQueueSpacesAvailable(NULL), 0);
	/* Should not crash */
	vQueueDelete(NULL);
}

/* --- Multi-task Queue Test --- */

static QueueHandle_t mt_queue;
static SemaphoreHandle_t qt_done_sem;

static void queue_producer(void *pvParameters)
{
	for (uint32_t i = 0; i < 5; i++) {
		xQueueSend(mt_queue, &i, portMAX_DELAY);
		vTaskDelay(pdMS_TO_TICKS(10));
	}
	vTaskDelete(NULL);
}

static void queue_consumer(void *pvParameters)
{
	uint32_t recv;

	for (int i = 0; i < 5; i++) {
		zassert_equal(xQueueReceive(mt_queue, &recv, portMAX_DELAY),
			      pdTRUE);
		zassert_equal(recv, (uint32_t)i, "Queue value mismatch");
	}
	xSemaphoreGive(qt_done_sem);
	vTaskDelete(NULL);
}

ZTEST(freertos_queue, test_queue_multitask)
{
	mt_queue = xQueueCreate(5, sizeof(uint32_t));
	qt_done_sem = xSemaphoreCreateBinary();
	zassert_not_null(mt_queue);
	zassert_not_null(qt_done_sem);

	xTaskCreate(queue_consumer, "QCons", configMINIMAL_STACK_SIZE,
		    NULL, 3, NULL);
	xTaskCreate(queue_producer, "QProd", configMINIMAL_STACK_SIZE,
		    NULL, 2, NULL);

	xSemaphoreTake(qt_done_sem, portMAX_DELAY);

	vQueueDelete(mt_queue);
	vSemaphoreDelete(qt_done_sem);
}

/* --- xQueueSendToFront (limited) --- */

ZTEST(freertos_queue, test_queue_send_to_front)
{
	QueueHandle_t q = xQueueCreate(3, sizeof(uint32_t));

	zassert_not_null(q);

	uint32_t val = 1;

	/* xQueueSendToFront maps to SendToBack with a warning */
	BaseType_t ret = xQueueSendToFront(q, &val, 0);

	zassert_equal(ret, pdTRUE, "SendToFront should succeed (maps to back)");

	uint32_t recv;

	xQueueReceive(q, &recv, 0);
	zassert_equal(recv, 1);

	vQueueDelete(q);
}

/* --- Max Allocation --- */

ZTEST(freertos_queue, test_queue_max_allocation)
{
	QueueHandle_t queues[CONFIG_FREERTOS_QUEUE_MAX_COUNT + 1];
	int i;

	for (i = 0; i <= CONFIG_FREERTOS_QUEUE_MAX_COUNT; i++) {
		queues[i] = xQueueCreate(2, sizeof(uint32_t));
		if (i == CONFIG_FREERTOS_QUEUE_MAX_COUNT) {
			zassert_is_null(queues[i],
					"Should fail after max count");
		} else {
			zassert_not_null(queues[i],
					 "Allocation %d should succeed", i);
		}
	}

	for (i = 0; i < CONFIG_FREERTOS_QUEUE_MAX_COUNT; i++) {
		vQueueDelete(queues[i]);
	}
}

ZTEST_SUITE(freertos_queue, NULL, NULL, NULL, NULL, NULL);
