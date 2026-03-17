/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_FREERTOS_H_
#define ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_FREERTOS_H_

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Base types */
typedef int32_t BaseType_t;
typedef uint32_t UBaseType_t;
typedef uint32_t TickType_t;

/* Boolean constants */
#define pdTRUE  ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS  ((BaseType_t)1)
#define pdFAIL  ((BaseType_t)0)

/* Maximum delay (maps to blocking forever) */
#define portMAX_DELAY ((TickType_t)UINT32_MAX)

/* Configuration constants derived from Kconfig */
#define configMAX_PRIORITIES     CONFIG_FREERTOS_MAX_PRIORITIES
#define configMINIMAL_STACK_SIZE CONFIG_FREERTOS_TASK_DEFAULT_STACK_SIZE
#define configTICK_RATE_HZ       CONFIG_SYS_CLOCK_TICKS_PER_SEC

/* Tick/ms conversion macros */
#define pdMS_TO_TICKS(xTimeInMs) \
	((TickType_t)k_ms_to_ticks_ceil32((uint32_t)(xTimeInMs)))

/* Critical section macros */
#define taskENTER_CRITICAL()       do { unsigned int __key = irq_lock()
#define taskEXIT_CRITICAL()        irq_unlock(__key); } while (0)
#define taskDISABLE_INTERRUPTS()   irq_lock()
#define taskENABLE_INTERRUPTS()    irq_unlock(0)

/* Yield */
#define portYIELD() k_yield()

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_PORTABILITY_FREERTOS_FREERTOS_H_ */
