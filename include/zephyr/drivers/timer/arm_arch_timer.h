/*
 * Copyright (c) 2019 Carlo Caione <ccaione@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree derived macros for the ARM architected timer driver.
 * @ingroup clock_apis
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_TIMER_ARM_ARCH_TIMER_H_
#define ZEPHYR_INCLUDE_DRIVERS_TIMER_ARM_ARCH_TIMER_H_

#include <zephyr/dt-bindings/interrupt-controller/arm-gic.h>
#include <zephyr/types.h>
#include <zephyr/devicetree.h>

#if DT_HAS_COMPAT_STATUS_OKAY(arm_armv8_timer)
#define ARM_TIMER_NODE DT_INST(0, arm_armv8_timer)
#elif DT_HAS_COMPAT_STATUS_OKAY(arm_armv7_timer)
#define ARM_TIMER_NODE DT_INST(0, arm_armv7_timer)
#endif

/** Secure physical timer IRQ number */
#define ARM_TIMER_SECURE_IRQ		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 0, irq)
/** Non-secure physical timer IRQ number */
#define ARM_TIMER_NON_SECURE_IRQ	DT_IRQ_BY_IDX(ARM_TIMER_NODE, 1, irq)
/** Virtual timer IRQ number */
#define ARM_TIMER_VIRTUAL_IRQ		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 2, irq)
/** Hypervisor timer IRQ number */
#define ARM_TIMER_HYP_IRQ		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 3, irq)

/** Secure physical timer IRQ priority */
#define ARM_TIMER_SECURE_PRIO		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 0,\
					priority)
/** Non-secure physical timer IRQ priority */
#define ARM_TIMER_NON_SECURE_PRIO	DT_IRQ_BY_IDX(ARM_TIMER_NODE, 1,\
					priority)
/** Virtual timer IRQ priority */
#define ARM_TIMER_VIRTUAL_PRIO		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 2,\
					priority)
/** Hypervisor timer IRQ priority */
#define ARM_TIMER_HYP_PRIO		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 3,\
					priority)

/** Secure physical timer IRQ flags */
#define ARM_TIMER_SECURE_FLAGS		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 0, flags)
/** Non-secure physical timer IRQ flags */
#define ARM_TIMER_NON_SECURE_FLAGS	DT_IRQ_BY_IDX(ARM_TIMER_NODE, 1, flags)
/** Virtual timer IRQ flags */
#define ARM_TIMER_VIRTUAL_FLAGS		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 2, flags)
/** Hypervisor timer IRQ flags */
#define ARM_TIMER_HYP_FLAGS		DT_IRQ_BY_IDX(ARM_TIMER_NODE, 3, flags)

#endif /* ZEPHYR_INCLUDE_DRIVERS_TIMER_ARM_ARCH_TIMER_H_ */
