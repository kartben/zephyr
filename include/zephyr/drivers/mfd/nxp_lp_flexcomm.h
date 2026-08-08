/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the NXP LP Flexcomm MFD driver.
 * @ingroup mfd_interfaces
 */

#ifndef ZEPHYR_DRIVERS_NXP_LP_FLEXCOMM_H_
#define ZEPHYR_DRIVERS_NXP_LP_FLEXCOMM_H_

#include "fsl_lpflexcomm.h"

/**
 * @brief Interrupt handler for an LP Flexcomm child device.
 *
 * @param dev Child device the interrupt is dispatched to.
 */
typedef void (*child_isr_t)(const struct device *dev);

/**
 * @brief Register an interrupt handler for an LP Flexcomm child device.
 *
 * The handler is called with @p child_dev as argument when an interrupt
 * of the given peripheral is dispatched by the LP Flexcomm.
 *
 * @param dev LP Flexcomm device.
 * @param child_dev Child device passed to the handler.
 * @param periph Peripheral function of the child device.
 * @param handler Interrupt handler to register.
 */
void nxp_lp_flexcomm_setirqhandler(const struct device *dev, const struct device *child_dev,
				   LP_FLEXCOMM_PERIPH_T periph, child_isr_t handler);

#endif /* ZEPHYR_DRIVERS_NXP_LP_FLEXCOMM_H_ */
