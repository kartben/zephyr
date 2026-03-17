/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_QUEUE_H_
#define ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_QUEUE_H_

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Error codes */
#define errQUEUE_FULL  ((BaseType_t)0)
#define errQUEUE_EMPTY ((BaseType_t)0)

/* Opaque queue handle */
typedef void *QueueHandle_t;

/**
 * @brief Create a new queue.
 *
 * @param uxQueueLength Maximum number of items the queue can hold.
 * @param uxItemSize    Size of each item in bytes.
 * @return Queue handle, or NULL on failure.
 */
QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize);

/**
 * @brief Send an item to the back of the queue.
 *
 * @param xQueue       Queue handle.
 * @param pvItemToQueue Pointer to the item to copy into the queue.
 * @param xTicksToWait Timeout in ticks, or portMAX_DELAY.
 * @return pdTRUE on success, errQUEUE_FULL on timeout.
 */
BaseType_t xQueueSend(QueueHandle_t xQueue, const void *pvItemToQueue,
		      TickType_t xTicksToWait);

/** @brief Alias for xQueueSend. */
#define xQueueSendToBack(xQueue, pvItemToQueue, xTicksToWait) \
	xQueueSend((xQueue), (pvItemToQueue), (xTicksToWait))

/**
 * @brief Send an item to the front of the queue.
 *
 * @note Zephyr's k_msgq is FIFO-only. This function behaves identically
 *       to xQueueSend (sends to back) and logs a warning on first call.
 */
BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void *pvItemToQueue,
			     TickType_t xTicksToWait);

/**
 * @brief Receive an item from the queue.
 *
 * @param xQueue       Queue handle.
 * @param pvBuffer     Buffer to copy the received item into.
 * @param xTicksToWait Timeout in ticks, or portMAX_DELAY.
 * @return pdTRUE on success, pdFALSE on timeout.
 */
BaseType_t xQueueReceive(QueueHandle_t xQueue, void *pvBuffer,
			 TickType_t xTicksToWait);

/**
 * @brief Delete a queue.
 */
void vQueueDelete(QueueHandle_t xQueue);

/**
 * @brief Get the number of items currently in the queue.
 */
UBaseType_t uxQueueMessagesWaiting(QueueHandle_t xQueue);

/**
 * @brief Get the number of free spaces in the queue.
 */
UBaseType_t uxQueueSpacesAvailable(QueueHandle_t xQueue);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_QUEUE_H_ */
