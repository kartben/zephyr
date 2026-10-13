/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * ESP-DL computes with the FPU and with the ESP32-S3 SIMD instructions (PIE),
 * coprocessors 0 and 3 of the core. The ESP32-S3 boot path leaves both
 * disabled in CPENABLE, where their first instruction faults, so enable them.
 *
 * Zephyr saves the FPU registers of a thread only with FPU_SHARING, and never
 * the PIE ones: run ESP-DL from a single thread.
 */

#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#include <xtensa/config/tie.h>

static int esp_dl_cpu_init(void)
{
	unsigned int cp;

	__asm__ volatile("rsr.cpenable %0" : "=r"(cp));
	cp |= BIT(XCHAL_CP_ID_FPU) | BIT(XCHAL_CP_ID_COP_AI);
	__asm__ volatile("wsr.cpenable %0; rsync" ::"r"(cp));

	return 0;
}

SYS_INIT(esp_dl_cpu_init, PRE_KERNEL_1, 0);
