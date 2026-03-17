/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TASK_H_
#define ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TASK_H_

#include "FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque task handle */
typedef void *TaskHandle_t;

/* Task function prototype */
typedef void (*TaskFunction_t)(void *);

/**
 * @brief Create a new task.
 *
 * @note usStackDepth is in bytes (differs from upstream FreeRTOS which uses words).
 *
 * @param pxTaskCode     Task entry function.
 * @param pcName         Task name string.
 * @param usStackDepth   Stack size in bytes.
 * @param pvParameters   Parameter passed to the task function.
 * @param uxPriority     FreeRTOS priority (0 = lowest).
 * @param pxCreatedTask  Optional pointer to receive the task handle.
 * @return pdPASS on success, pdFAIL on failure.
 */
BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *pcName,
		       uint16_t usStackDepth, void *pvParameters,
		       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask);

/**
 * @brief Delete a task.
 *
 * @param xTaskToDelete Task handle, or NULL to delete the calling task.
 */
void vTaskDelete(TaskHandle_t xTaskToDelete);

/**
 * @brief Delay the calling task for a number of ticks.
 */
void vTaskDelay(const TickType_t xTicksToDelay);

/**
 * @brief Delay until an absolute tick count.
 *
 * @param pxPreviousWakeTime Pointer to the last wake time (updated by this function).
 * @param xTimeIncrement     Tick increment for periodic delay.
 * @return pdTRUE if the task was delayed, pdFALSE if the next wake time has already passed.
 */
BaseType_t xTaskDelayUntil(TickType_t *pxPreviousWakeTime,
			   const TickType_t xTimeIncrement);

/** @brief Yield the processor. */
#define taskYIELD() portYIELD()

/**
 * @brief Get the priority of a task.
 *
 * @param xTask Task handle, or NULL for the calling task.
 * @return FreeRTOS priority level.
 */
UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask);

/**
 * @brief Set the priority of a task.
 *
 * @param xTask       Task handle, or NULL for the calling task.
 * @param uxNewPriority New FreeRTOS priority level.
 */
void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority);

/**
 * @brief Get the handle of the currently running task.
 */
TaskHandle_t xTaskGetCurrentTaskHandle(void);

/**
 * @brief Get the current tick count.
 */
TickType_t xTaskGetTickCount(void);

/**
 * @brief Start the scheduler. No-op on Zephyr (scheduler starts automatically).
 */
void vTaskStartScheduler(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_TASK_H_ */
