/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <task.h>
#include <string.h>
#include "freertos_types.h"

K_MEM_SLAB_DEFINE(freertos_task_cb_slab, sizeof(struct freertos_task_cb),
		  CONFIG_FREERTOS_TASK_MAX_COUNT, 4);

K_THREAD_STACK_ARRAY_DEFINE(freertos_task_stacks,
			    CONFIG_FREERTOS_TASK_MAX_COUNT,
			    CONFIG_FREERTOS_TASK_DEFAULT_STACK_SIZE);

/* Bitmap tracking which stack slots are in use */
static atomic_t stack_slot_used[CONFIG_FREERTOS_TASK_MAX_COUNT];

static void task_entry_wrapper(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p3);

	TaskFunction_t func = (TaskFunction_t)p1;

	func(p2);
}

static int alloc_stack_slot(void)
{
	for (int i = 0; i < CONFIG_FREERTOS_TASK_MAX_COUNT; i++) {
		if (atomic_cas(&stack_slot_used[i], 0, 1)) {
			return i;
		}
	}
	return -1;
}

static void free_stack_slot(int idx)
{
	if (idx >= 0 && idx < CONFIG_FREERTOS_TASK_MAX_COUNT) {
		atomic_set(&stack_slot_used[idx], 0);
	}
}

BaseType_t xTaskCreate(TaskFunction_t pxTaskCode, const char *pcName,
		       uint16_t usStackDepth, void *pvParameters,
		       UBaseType_t uxPriority, TaskHandle_t *pxCreatedTask)
{
	struct freertos_task_cb *task;
	int idx;
	int zephyr_prio;

	if (pxTaskCode == NULL) {
		return pdFAIL;
	}

	if (k_mem_slab_alloc(&freertos_task_cb_slab, (void **)&task,
			     K_MSEC(100)) != 0) {
		return pdFAIL;
	}

	idx = alloc_stack_slot();
	if (idx < 0) {
		k_mem_slab_free(&freertos_task_cb_slab, (void *)task);
		return pdFAIL;
	}

	memset(task, 0, sizeof(struct freertos_task_cb));
	task->is_dynamic = true;
	task->stack_idx = idx;

	zephyr_prio = freertos_to_zephyr_priority(uxPriority);

	k_thread_create(&task->z_thread,
			freertos_task_stacks[idx],
			CONFIG_FREERTOS_TASK_DEFAULT_STACK_SIZE,
			task_entry_wrapper,
			(void *)pxTaskCode, pvParameters, NULL,
			zephyr_prio, 0, K_NO_WAIT);

	if (pcName != NULL) {
		k_thread_name_set(&task->z_thread, pcName);
	}

	if (pxCreatedTask != NULL) {
		*pxCreatedTask = (TaskHandle_t)task;
	}

	return pdPASS;
}

void vTaskDelete(TaskHandle_t xTaskToDelete)
{
	struct freertos_task_cb *task = (struct freertos_task_cb *)xTaskToDelete;

	if (task == NULL) {
		/* Delete calling task */
		k_thread_abort(k_current_get());
		return;
	}

	k_thread_abort(&task->z_thread);

	if (task->is_dynamic) {
		free_stack_slot(task->stack_idx);
		k_mem_slab_free(&freertos_task_cb_slab, (void *)task);
	}
}

void vTaskDelay(const TickType_t xTicksToDelay)
{
	if (xTicksToDelay == 0U) {
		k_yield();
	} else {
		k_sleep(K_TICKS(xTicksToDelay));
	}
}

BaseType_t xTaskDelayUntil(TickType_t *pxPreviousWakeTime,
			   const TickType_t xTimeIncrement)
{
	TickType_t now = xTaskGetTickCount();
	TickType_t next_wake = *pxPreviousWakeTime + xTimeIncrement;

	if (next_wake > now) {
		k_sleep(K_TICKS(next_wake - now));
		*pxPreviousWakeTime = next_wake;
		return pdTRUE;
	}

	/* Already past the target time */
	*pxPreviousWakeTime = now;
	return pdFALSE;
}

UBaseType_t uxTaskPriorityGet(TaskHandle_t xTask)
{
	struct freertos_task_cb *task = (struct freertos_task_cb *)xTask;
	int zephyr_prio;

	if (task == NULL) {
		zephyr_prio = k_thread_priority_get(k_current_get());
	} else {
		zephyr_prio = k_thread_priority_get(&task->z_thread);
	}

	return zephyr_to_freertos_priority(zephyr_prio);
}

void vTaskPrioritySet(TaskHandle_t xTask, UBaseType_t uxNewPriority)
{
	struct freertos_task_cb *task = (struct freertos_task_cb *)xTask;
	int zephyr_prio = freertos_to_zephyr_priority(uxNewPriority);

	if (task == NULL) {
		k_thread_priority_set(k_current_get(), zephyr_prio);
	} else {
		k_thread_priority_set(&task->z_thread, zephyr_prio);
	}
}

TaskHandle_t xTaskGetCurrentTaskHandle(void)
{
	/* Return the Zephyr thread pointer directly cast as TaskHandle_t.
	 * This handle is usable with vTaskDelete but not with priority
	 * functions (which require a freertos_task_cb). For tasks created
	 * via xTaskCreate, the k_thread is the first member of the CB,
	 * so the pointer is interchangeable.
	 */
	return (TaskHandle_t)CONTAINER_OF(k_current_get(),
					  struct freertos_task_cb, z_thread);
}
