/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SUBSYS_PORTABILITY_FREERTOS_TYPES_H_
#define ZEPHYR_SUBSYS_PORTABILITY_FREERTOS_TYPES_H_

#include <zephyr/kernel.h>
#include <timers.h>

#ifdef __cplusplus
extern "C" {
#endif

enum freertos_sem_type {
	FREERTOS_SEM_BINARY,
	FREERTOS_SEM_COUNTING,
	FREERTOS_SEM_MUTEX,
};

struct freertos_task_cb {
	struct k_thread z_thread;
	int stack_idx;
	bool is_dynamic;
};

struct freertos_sem_cb {
	enum freertos_sem_type type;
	union {
		struct k_sem z_sem;
		struct k_mutex z_mutex;
	};
	bool is_dynamic;
};

struct freertos_queue_cb {
	struct k_msgq z_msgq;
	void *buffer;
	bool is_dynamic;
	bool is_buffer_dynamic;
};

struct freertos_timer_cb {
	struct k_timer z_timer;
	TimerCallbackFunction_t callback;
	void *timer_id;
	const char *name;
	TickType_t period;
	UBaseType_t auto_reload;
	bool is_active;
	bool is_dynamic;
};

/* Priority conversion helpers */
static inline int freertos_to_zephyr_priority(UBaseType_t freertos_prio)
{
	return (int)(CONFIG_FREERTOS_MAX_PRIORITIES - 1 - freertos_prio);
}

static inline UBaseType_t zephyr_to_freertos_priority(int zephyr_prio)
{
	return (UBaseType_t)(CONFIG_FREERTOS_MAX_PRIORITIES - 1 - zephyr_prio);
}

/* Timeout conversion helper */
static inline k_timeout_t freertos_to_zephyr_timeout(TickType_t ticks)
{
	if (ticks == portMAX_DELAY) {
		return K_FOREVER;
	} else if (ticks == 0U) {
		return K_NO_WAIT;
	}
	return K_TICKS(ticks);
}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SUBSYS_PORTABILITY_FREERTOS_TYPES_H_ */
