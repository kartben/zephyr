/*
 * Copyright (c) 2016-2024 Nordic Semiconductor ASA
 * Copyright (c) 2016 Vinayak Kariappa Chettimada
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RV32M1_CPU_H_
#define ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RV32M1_CPU_H_

static inline void cpu_sleep(void)
{
	__WFE();
	/* __SEV(); */
	__WFE();
}

static inline void cpu_dmb(void)
{
	/* FIXME: Add necessary host machine required Data Memory Barrier
	 *        instruction along with the below defined compiler memory
	 *        clobber.
	 */
	__asm__ volatile ("" : : : "memory");
}

#endif /* ZEPHYR_BT_CONTROLLER_LL_SW_OPENISA_HAL_RV32M1_CPU_H_ */
