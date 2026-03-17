/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file Queue-based producer/consumer demo using FreeRTOS compatibility APIs.
 *
 * A periodic software timer sends incrementing values to a queue.
 * A receiver task reads from the queue and prints the values.
 */

#include <FreeRTOS.h>
#include <task.h>
#include <queue.h>
#include <timers.h>

#define QUEUE_LENGTH  5
#define ITEM_SIZE     sizeof(uint32_t)
#define NUM_MESSAGES  10
#define TIMER_PERIOD  pdMS_TO_TICKS(100)

static QueueHandle_t msg_queue;
static volatile uint32_t send_count;

static void timer_callback(TimerHandle_t xTimer)
{
	ARG_UNUSED(xTimer);

	uint32_t value = send_count++;

	if (value < NUM_MESSAGES) {
		if (xQueueSend(msg_queue, &value, 0) == pdTRUE) {
			printk("Timer: sent %u to queue\n", value);
		} else {
			printk("Timer: queue full, dropping %u\n", value);
		}
	}
}

static void receiver_task(void *pvParameters)
{
	ARG_UNUSED(pvParameters);
	uint32_t received;
	uint32_t count = 0;

	while (count < NUM_MESSAGES) {
		if (xQueueReceive(msg_queue, &received,
				  portMAX_DELAY) == pdTRUE) {
			printk("Receiver: got %u from queue\n", received);
			count++;
		}
	}

	printk("Sample execution successful\n");
	vTaskDelete(NULL);
}

int main(void)
{
	TimerHandle_t timer;

	msg_queue = xQueueCreate(QUEUE_LENGTH, ITEM_SIZE);
	if (msg_queue == NULL) {
		printk("Error: failed to create queue\n");
		return 1;
	}

	if (xTaskCreate(receiver_task, "Receiver", configMINIMAL_STACK_SIZE,
			NULL, 2, NULL) != pdPASS) {
		printk("Error: failed to create receiver task\n");
		return 1;
	}

	timer = xTimerCreate("SendTimer", TIMER_PERIOD, pdTRUE, NULL,
			     timer_callback);
	if (timer == NULL) {
		printk("Error: failed to create timer\n");
		return 1;
	}

	xTimerStart(timer, 0);

	return 0;
}
