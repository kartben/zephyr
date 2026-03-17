/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <semphr.h>
#include <string.h>
#include "freertos_types.h"

K_MEM_SLAB_DEFINE(freertos_sem_cb_slab, sizeof(struct freertos_sem_cb),
		  CONFIG_FREERTOS_SEMAPHORE_MAX_COUNT, 4);

K_MEM_SLAB_DEFINE(freertos_mutex_cb_slab, sizeof(struct freertos_sem_cb),
		  CONFIG_FREERTOS_MUTEX_MAX_COUNT, 4);

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
	struct freertos_sem_cb *sem;

	if (k_mem_slab_alloc(&freertos_sem_cb_slab, (void **)&sem,
			     K_MSEC(100)) != 0) {
		return NULL;
	}

	memset(sem, 0, sizeof(*sem));
	sem->type = FREERTOS_SEM_BINARY;
	sem->is_dynamic = true;
	k_sem_init(&sem->z_sem, 0, 1);

	return (SemaphoreHandle_t)sem;
}

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t uxMaxCount,
					   UBaseType_t uxInitialCount)
{
	struct freertos_sem_cb *sem;

	if (k_mem_slab_alloc(&freertos_sem_cb_slab, (void **)&sem,
			     K_MSEC(100)) != 0) {
		return NULL;
	}

	memset(sem, 0, sizeof(*sem));
	sem->type = FREERTOS_SEM_COUNTING;
	sem->is_dynamic = true;
	k_sem_init(&sem->z_sem, uxInitialCount, uxMaxCount);

	return (SemaphoreHandle_t)sem;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
	struct freertos_sem_cb *sem;

	if (k_is_in_isr()) {
		return NULL;
	}

	if (k_mem_slab_alloc(&freertos_mutex_cb_slab, (void **)&sem,
			     K_MSEC(100)) != 0) {
		return NULL;
	}

	memset(sem, 0, sizeof(*sem));
	sem->type = FREERTOS_SEM_MUTEX;
	sem->is_dynamic = true;
	k_mutex_init(&sem->z_mutex);

	return (SemaphoreHandle_t)sem;
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t xSemaphore, TickType_t xTicksToWait)
{
	struct freertos_sem_cb *sem = (struct freertos_sem_cb *)xSemaphore;
	k_timeout_t timeout;
	int status;

	if (sem == NULL) {
		return pdFALSE;
	}

	timeout = freertos_to_zephyr_timeout(xTicksToWait);

	if (sem->type == FREERTOS_SEM_MUTEX) {
		if (k_is_in_isr()) {
			return pdFALSE;
		}
		status = k_mutex_lock(&sem->z_mutex, timeout);
	} else {
		status = k_sem_take(&sem->z_sem, timeout);
	}

	return (status == 0) ? pdTRUE : pdFALSE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t xSemaphore)
{
	struct freertos_sem_cb *sem = (struct freertos_sem_cb *)xSemaphore;

	if (sem == NULL) {
		return pdFALSE;
	}

	if (sem->type == FREERTOS_SEM_MUTEX) {
		if (k_is_in_isr()) {
			return pdFALSE;
		}
		return (k_mutex_unlock(&sem->z_mutex) == 0) ? pdTRUE : pdFALSE;
	}

	k_sem_give(&sem->z_sem);
	return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t xSemaphore)
{
	struct freertos_sem_cb *sem = (struct freertos_sem_cb *)xSemaphore;

	if (sem == NULL) {
		return;
	}

	if (sem->is_dynamic) {
		if (sem->type == FREERTOS_SEM_MUTEX) {
			k_mem_slab_free(&freertos_mutex_cb_slab, (void *)sem);
		} else {
			k_mem_slab_free(&freertos_sem_cb_slab, (void *)sem);
		}
	}
}
