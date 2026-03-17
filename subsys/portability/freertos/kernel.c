/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <task.h>

void vTaskStartScheduler(void)
{
	/* No-op: Zephyr's scheduler is already running when main() executes. */
}

TickType_t xTaskGetTickCount(void)
{
	return (TickType_t)sys_clock_tick_get_32();
}
