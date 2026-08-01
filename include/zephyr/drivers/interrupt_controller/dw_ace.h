/*
 * Copyright (c) 2022 Intel Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Intel ACE DesignWare interrupt controller driver API.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_DW_ACE_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_DW_ACE_H_

#include <zephyr/device.h>

/**
 * @brief Enable an interrupt line.
 *
 * @param dev Interrupt controller device.
 * @param irq Interrupt line to enable.
 */
typedef void (*irq_enable_t)(const struct device *dev, uint32_t irq);

/**
 * @brief Disable an interrupt line.
 *
 * @param dev Interrupt controller device.
 * @param irq Interrupt line to disable.
 */
typedef void (*irq_disable_t)(const struct device *dev, uint32_t irq);

/**
 * @brief Check if an interrupt line is enabled.
 *
 * @param dev Interrupt controller device.
 * @param irq Interrupt line to check.
 *
 * @return Non-zero if the interrupt line is enabled, zero otherwise.
 */
typedef int (*irq_is_enabled_t)(const struct device *dev, unsigned int irq);

/**
 * @brief Connect an ISR to an interrupt line at runtime.
 *
 * @param dev Interrupt controller device.
 * @param irq Interrupt line.
 * @param priority Interrupt priority.
 * @param routine ISR to call when the interrupt is triggered.
 * @param parameter Parameter passed to the ISR.
 * @param flags IRQ configuration flags.
 *
 * @return The vector assigned to this interrupt.
 */
typedef int (*irq_connect_dynamic_t)(const struct device *dev,
				     unsigned int irq, unsigned int priority,
				     void (*routine)(const void *parameter),
				     const void *parameter, uint32_t flags);

/**
 * @brief DesignWare ACE interrupt controller driver API.
 */
struct dw_ace_v1_ictl_driver_api {
	irq_enable_t intr_enable; /**< Enable an interrupt line. */
	irq_disable_t intr_disable; /**< Disable an interrupt line. */
	irq_is_enabled_t intr_is_enabled; /**< Check if an interrupt line is enabled. */
#ifdef CONFIG_DYNAMIC_INTERRUPTS
	irq_connect_dynamic_t intr_connect_dynamic; /**< Connect an ISR at runtime. */
#endif
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_DW_ACE_H_ */
