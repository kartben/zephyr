/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_SEMPHR_H_
#define ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_SEMPHR_H_

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque semaphore/mutex handle */
typedef void *SemaphoreHandle_t;

/**
 * @brief Create a binary semaphore (initial count 0, max 1).
 */
SemaphoreHandle_t xSemaphoreCreateBinary(void);

/**
 * @brief Create a counting semaphore.
 *
 * @param uxMaxCount     Maximum count value.
 * @param uxInitialCount Initial count value.
 */
SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
					   UBaseType_t uxInitialCount);

/**
 * @brief Create a mutex.
 */
SemaphoreHandle_t xSemaphoreCreateMutex(void);

/**
 * @brief Take (acquire) a semaphore or mutex.
 *
 * @param xSemaphore    Semaphore/mutex handle.
 * @param xTicksToWait  Timeout in ticks, or portMAX_DELAY.
 * @return pdTRUE if acquired, pdFALSE on timeout.
 */
BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore,
			  TickType_t xTicksToWait);

/**
 * @brief Give (release) a semaphore or mutex.
 *
 * @param xSemaphore Semaphore/mutex handle.
 * @return pdTRUE on success, pdFALSE on failure.
 */
BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore);

/**
 * @brief Delete a semaphore or mutex.
 */
void vSemaphoreDelete(SemaphoreHandle_t xSemaphore);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_SEMPHR_H_ */
