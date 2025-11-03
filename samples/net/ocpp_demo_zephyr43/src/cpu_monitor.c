/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#ifdef CONFIG_CPU_LOAD
#include <zephyr/sys/cpu_load.h>
#endif

#include "ocpp_demo.h"

LOG_MODULE_REGISTER(cpu_monitor, LOG_LEVEL_INF);

static uint8_t current_cpu_load;
static struct k_timer cpu_load_timer;

#ifdef CONFIG_CPU_LOAD
static void cpu_load_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	
	uint32_t load;
	int ret = cpu_load_get(&load);
	
	if (ret == 0) {
		/* cpu_load_get returns value in 0-10000 range (0.00% - 100.00%) */
		current_cpu_load = (uint8_t)(load / 100);
		LOG_DBG("CPU Load: %u%%", current_cpu_load);
	} else {
		LOG_WRN("Failed to get CPU load: %d", ret);
	}
}
#endif

void cpu_monitor_init(void)
{
#ifdef CONFIG_CPU_LOAD
	LOG_INF("CPU load monitoring initialized");
	
	k_timer_init(&cpu_load_timer, cpu_load_timer_handler, NULL);
	k_timer_start(&cpu_load_timer, K_SECONDS(1), K_SECONDS(1));
	
	current_cpu_load = 0;
#else
	LOG_WRN("CPU load monitoring not available (CONFIG_CPU_LOAD not enabled)");
	current_cpu_load = 0;
#endif
}

uint8_t cpu_monitor_get_load(void)
{
	return current_cpu_load;
}

uint32_t cpu_monitor_get_freq(void)
{
	/* For STM32F746, the CPU frequency is typically 216 MHz
	 * CPU frequency scaling might not be available on all platforms
	 */
#ifdef CONFIG_CPU_FREQ
	/* If CPU freq scaling is available, query current frequency */
	/* This is platform-specific - implementation would go here */
	return 216; /* Default for STM32F746 */
#else
	/* Return fixed frequency */
	return 216; /* STM32F746 @ 216 MHz */
#endif
}
