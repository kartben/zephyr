/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <queue.h>
#include <string.h>
#include "freertos_types.h"

LOG_MODULE_REGISTER(freertos_queue, LOG_LEVEL_WRN);

K_MEM_SLAB_DEFINE(freertos_queue_cb_slab, sizeof(struct freertos_queue_cb),
		  CONFIG_FREERTOS_QUEUE_MAX_COUNT, 4);

QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
	struct freertos_queue_cb *queue;
	void *buffer;

	if (uxQueueLength == 0 || uxItemSize == 0) {
		return NULL;
	}

	if (k_mem_slab_alloc(&freertos_queue_cb_slab, (void **)&queue,
			     K_MSEC(100)) != 0) {
		return NULL;
	}

	buffer = k_calloc(uxQueueLength, uxItemSize);
	if (buffer == NULL) {
		k_mem_slab_free(&freertos_queue_cb_slab, (void *)queue);
		return NULL;
	}

	memset(queue, 0, sizeof(*queue));
	queue->is_dynamic = true;
	queue->is_buffer_dynamic = true;
	queue->buffer = buffer;

	k_msgq_init(&queue->z_msgq, buffer, uxItemSize, uxQueueLength);

	return (QueueHandle_t)queue;
}

BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		      TickType_t xTicksToWait)
{
	struct freertos_queue_cb *queue = (struct freertos_queue_cb *)xQueue;
	int status;

	if (queue == NULL || pvItemToQueue == NULL) {
		return errQUEUE_FULL;
	}

	status = k_msgq_put(&queue->z_msgq, pvItemToQueue,
			    freertos_to_zephyr_timeout(xTicksToWait));

	return (status == 0) ? pdTRUE : errQUEUE_FULL;
}

BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
			     TickType_t xTicksToWait)
{
	LOG_WRN("xQueueSendToFront: Zephyr k_msgq is FIFO-only, "
		"sending to back instead");
	return xQueueSend(xQueue, pvItemToQueue, xTicksToWait);
}

BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait)
{
	struct freertos_queue_cb *queue = (struct freertos_queue_cb *)xQueue;
	int status;

	if (queue == NULL || pvBuffer == NULL) {
		return pdFALSE;
	}

	status = k_msgq_get(&queue->z_msgq, pvBuffer,
			    freertos_to_zephyr_timeout(xTicksToWait));

	return (status == 0) ? pdTRUE : pdFALSE;
}

void vQueueDelete(QueueHandle_t xQueue)
{
	struct freertos_queue_cb *queue = (struct freertos_queue_cb *)xQueue;

	if (queue == NULL) {
		return;
	}

	k_msgq_purge(&queue->z_msgq);

	if (queue->is_buffer_dynamic) {
		k_free(queue->buffer);
	}

	if (queue->is_dynamic) {
		k_mem_slab_free(&freertos_queue_cb_slab, (void *)queue);
	}
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue)
{
	struct freertos_queue_cb *queue = (struct freertos_queue_cb *)xQueue;

	if (queue == NULL) {
		return 0;
	}

	return (UBaseType_t)k_msgq_num_used_get(&queue->z_msgq);
}

UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue)
{
	struct freertos_queue_cb *queue = (struct freertos_queue_cb *)xQueue;

	if (queue == NULL) {
		return 0;
	}

	return (UBaseType_t)k_msgq_num_free_get(&queue->z_msgq);
}
