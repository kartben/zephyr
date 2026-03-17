/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TIMERS_H_
#define ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TIMERS_H_

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque timer handle */
typedef void *TimerHandle_t;

/* Timer callback function type */
typedef void (*TimerCallbackFunction_t)(TimerHandle_t xTimer);

/**
 * @brief Create a software timer.
 *
 * @param pcTimerName     Human-readable timer name.
 * @param xTimerPeriod    Timer period in ticks.
 * @param uxAutoReload    pdTRUE for periodic, pdFALSE for one-shot.
 * @param pvTimerID       Application-defined timer ID.
 * @param pxCallbackFunction Timer expiry callback.
 * @return Timer handle, or NULL on failure.
 */
TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
			   UBaseType_t uxAutoReload, void *pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction);

/**
 * @brief Start a timer.
 *
 * @param xTimer        Timer handle.
 * @param xTicksToWait  Ignored (no command queue in this implementation).
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait);

/**
 * @brief Stop a timer.
 *
 * @param xTimer        Timer handle.
 * @param xTicksToWait  Ignored.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait);

/**
 * @brief Delete a timer.
 *
 * @param xTimer        Timer handle.
 * @param xTicksToWait  Ignored.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait);

/**
 * @brief Change the period of a timer.
 *
 * If the timer is running, it is restarted with the new period.
 *
 * @param xTimer          Timer handle.
 * @param xNewPeriod      New period in ticks.
 * @param xTicksToWait    Ignored.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
			      TickType_t xTicksToWait);

/**
 * @brief Reset (restart) a timer.
 *
 * @param xTimer        Timer handle.
 * @param xTicksToWait  Ignored.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait);

/**
 * @brief Get the timer ID.
 *
 * @param xTimer Timer handle.
 * @return The pvTimerID value set at creation.
 */
void *pvTimerGetTimerID(TimerHandle_t xTimer);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TIMERS_H_ */
