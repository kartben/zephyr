/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <timers.h>
#include <string.h>
#include "freertos_types.h"

K_MEM_SLAB_DEFINE(freertos_timer_cb_slab, sizeof(struct freertos_timer_cb),
		  CONFIG_FREERTOS_TIMER_MAX_COUNT, 4);

static void timer_expiry_wrapper(struct k_timer *timer)
{
	struct freertos_timer_cb *cb =
		CONTAINER_OF(timer, struct freertos_timer_cb, z_timer);

	if (cb->callback != NULL) {
		cb->callback((TimerHandle_t)cb);
	}
}

TimerHandle_t xTimerCreate(const char *pcTimerName, TickType_t xTimerPeriod,
			   UBaseType_t uxAutoReload, void *pvTimerID,
			   TimerCallbackFunction_t pxCallbackFunction)
{
	struct freertos_timer_cb *timer;

	if (pxCallbackFunction == NULL) {
		return NULL;
	}

	if (k_is_in_isr()) {
		return NULL;
	}

	if (k_mem_slab_alloc(&freertos_timer_cb_slab, (void **)&timer,
			     K_MSEC(100)) != 0) {
		return NULL;
	}

	memset(timer, 0, sizeof(*timer));
	timer->is_dynamic = true;
	timer->callback = pxCallbackFunction;
	timer->timer_id = pvTimerID;
	timer->name = pcTimerName;
	timer->period = xTimerPeriod;
	timer->auto_reload = uxAutoReload;
	timer->is_active = false;

	k_timer_init(&timer->z_timer, timer_expiry_wrapper, NULL);

	return (TimerHandle_t)timer;
}

BaseType_t xTimerStart(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	struct freertos_timer_cb *timer = (struct freertos_timer_cb *)xTimer;

	ARG_UNUSED(xTicksToWait);

	if (timer == NULL) {
		return pdFAIL;
	}

	if (timer->auto_reload == pdTRUE) {
		k_timer_start(&timer->z_timer, K_TICKS(timer->period),
			      K_TICKS(timer->period));
	} else {
		k_timer_start(&timer->z_timer, K_TICKS(timer->period),
			      K_NO_WAIT);
	}

	timer->is_active = true;
	return pdPASS;
}

BaseType_t xTimerStop(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	struct freertos_timer_cb *timer = (struct freertos_timer_cb *)xTimer;

	ARG_UNUSED(xTicksToWait);

	if (timer == NULL) {
		return pdFAIL;
	}

	k_timer_stop(&timer->z_timer);
	timer->is_active = false;
	return pdPASS;
}

BaseType_t xTimerDelete(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	struct freertos_timer_cb *timer = (struct freertos_timer_cb *)xTimer;

	ARG_UNUSED(xTicksToWait);

	if (timer == NULL) {
		return pdFAIL;
	}

	if (timer->is_active) {
		k_timer_stop(&timer->z_timer);
		timer->is_active = false;
	}

	if (timer->is_dynamic) {
		k_mem_slab_free(&freertos_timer_cb_slab, (void *)timer);
	}

	return pdPASS;
}

BaseType_t xTimerChangePeriod(TimerHandle_t xTimer, TickType_t xNewPeriod,
			      TickType_t xTicksToWait)
{
	struct freertos_timer_cb *timer = (struct freertos_timer_cb *)xTimer;

	ARG_UNUSED(xTicksToWait);

	if (timer == NULL) {
		return pdFAIL;
	}

	timer->period = xNewPeriod;

	if (timer->is_active) {
		/* Restart with new period */
		if (timer->auto_reload == pdTRUE) {
			k_timer_start(&timer->z_timer, K_TICKS(xNewPeriod),
				      K_TICKS(xNewPeriod));
		} else {
			k_timer_start(&timer->z_timer, K_TICKS(xNewPeriod),
				      K_NO_WAIT);
		}
	}

	return pdPASS;
}

BaseType_t xTimerReset(TimerHandle_t xTimer, TickType_t xTicksToWait)
{
	/* Reset restarts the timer from the current moment */
	return xTimerStart(xTimer, xTicksToWait);
}

void *pvTimerGetTimerID(TimerHandle_t xTimer)
{
	struct freertos_timer_cb *timer = (struct freertos_timer_cb *)xTimer;

	if (timer == NULL) {
		return NULL;
	}

	return timer->timer_id;
}
