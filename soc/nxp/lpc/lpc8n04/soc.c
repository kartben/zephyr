/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>
#include <soc.h>
#include <fsl_clock.h>

#define LPC8N04_SFRO_HZ 8000000U
#define LPC8N04_CPU_HZ  DT_PROP(DT_PATH(cpus, cpu_0), clock_frequency)

/* The system clock is the 8 MHz SFRO divided by 2^SYSCLKDIV, SYSCLKDIV = 0..7. */
BUILD_ASSERT((LPC8N04_SFRO_HZ % LPC8N04_CPU_HZ) == 0U &&
		     IS_POWER_OF_TWO(LPC8N04_SFRO_HZ / LPC8N04_CPU_HZ) &&
		     (LPC8N04_SFRO_HZ / LPC8N04_CPU_HZ) <= 128U,
	     "cpu clock-frequency must be 8 MHz divided by a power of two up to 128");

void soc_early_init_hook(void)
{
	/* SYSCLKDIV resets to divide-by-16 (500 kHz). */
	CLOCK_SetSysClkDiv((clock_SysClkfrequency_t)LOG2(LPC8N04_SFRO_HZ / LPC8N04_CPU_HZ));
}
