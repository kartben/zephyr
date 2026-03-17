/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Task synchronization demo using FreeRTOS compatibility APIs.
 *
 * Two tasks communicate using a binary semaphore: a producer task
 * gives the semaphore, and a consumer task takes it.
 */

#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>

#define NUM_ITERATIONS 10
#define DELAY_TICKS    pdMS_TO_TICKS(100)

static SemaphoreHandle_t sync_sem;
static volatile int iteration;

static void producer_task(void *pvParameters)
{
	ARG_UNUSED(pvParameters);

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		vTaskDelay(DELAY_TICKS);
		iteration = i;
		printk("Producer: giving semaphore %d\n", i);
		xSemaphoreGive(sync_sem);
	}

	vTaskDelete(NULL);
}

static void consumer_task(void *pvParameters)
{
	ARG_UNUSED(pvParameters);

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		if (xSemaphoreTake(sync_sem, portMAX_DELAY) == pdTRUE) {
			printk("Consumer: got semaphore %d\n", iteration);
		}
	}

	printk("Sample execution successful\n");
	vTaskDelete(NULL);
}

int main(void)
{
	sync_sem = xSemaphoreCreateBinary();
	if (sync_sem == NULL) {
		printk("Error: failed to create semaphore\n");
		return 1;
	}

	if (xTaskCreate(consumer_task, "Consumer", configMINIMAL_STACK_SIZE,
			NULL, 2, NULL) != pdPASS) {
		printk("Error: failed to create consumer task\n");
		return 1;
	}

	if (xTaskCreate(producer_task, "Producer", configMINIMAL_STACK_SIZE,
			NULL, 1, NULL) != pdPASS) {
		printk("Error: failed to create producer task\n");
		return 1;
	}

	return 0;
}
