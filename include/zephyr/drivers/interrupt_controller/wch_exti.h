/*
 * Copyright (c) 2025 Michael Hope <michaelh@juju.nz>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the WCH EXTI interrupt controller driver API.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_WCH_EXTI_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_WCH_EXTI_H_

#include <stdint.h>

#include <zephyr/sys/util_macro.h>

/**
 * @brief Callback for EXTI interrupt
 *
 * @param line EXTI line that triggered the interrupt
 * @param user User data passed to wch_exti_configure()
 */
typedef void (*wch_exti_callback_handler_t)(uint8_t line, void *user);

/** EXTI trigger condition flags */
enum wch_exti_trigger {
	/*
	 * Note that this is a flag set and these values can be ORed to trigger on
	 * both edges.
	 */

	/** Trigger on rising edge */
	WCH_EXTI_TRIGGER_RISING_EDGE = BIT(0),
	/** Trigger on falling edge */
	WCH_EXTI_TRIGGER_FALLING_EDGE = BIT(1),
};

/**
 * @brief Enable the EXTI interrupt for a line
 *
 * @param line EXTI line
 */
void wch_exti_enable(uint8_t line);

/**
 * @brief Disable the EXTI interrupt for a line
 *
 * @param line EXTI line
 */
void wch_exti_disable(uint8_t line);

/**
 * @brief Set the trigger mode for a line
 *
 * @param line EXTI line
 * @param trigger Trigger condition flags
 */
void wch_exti_set_trigger(uint8_t line, enum wch_exti_trigger trigger);

/**
 * @brief Register a callback for a line
 *
 * Pass a NULL @p callback to unregister the current callback.
 *
 * @param line EXTI line
 * @param callback Callback to invoke when the interrupt triggers, or NULL
 * @param user User data passed to the callback
 *
 * @retval 0 on success
 * @retval -EALREADY if a different callback or user data is already registered
 */
int wch_exti_configure(uint8_t line, wch_exti_callback_handler_t callback, void *user);

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_WCH_EXTI_H_ */
