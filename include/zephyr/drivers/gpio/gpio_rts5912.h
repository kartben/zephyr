/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2024 Realtek Semiconductor Corporation, SIBG-SD7
 * Author: Lin Yu-Cheng <lin_yu_cheng@realtek.com>
 */

/**
 * @file
 * @brief Header file for the Realtek RTS5912 GPIO helper functions.
 * @ingroup gpio_interface_ext
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_GPIO_GPIO_RTS5912_H_
#define ZEPHYR_INCLUDE_DRIVERS_GPIO_GPIO_RTS5912_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <reg/reg_gpio.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the first pin of a GPIO port with a pending interrupt.
 *
 * @param reg_base Base address of the per-pin control (GCR) registers of the port
 *
 * @return Index of the first pin with a pending interrupt, or 16 if none is pending
 */
gpio_pin_t gpio_rts5912_get_intr_pin(volatile uint32_t *reg_base);

/**
 * @brief Configure a pin as a GPIO input with interrupt enabled so that it can
 * wake up the system.
 *
 * @param pin_num SoC-wide pin number, as returned by gpio_rts5912_get_pin_num()
 */
static ALWAYS_INLINE void gpio_rts5912_set_wakeup_pin(uint32_t pin_num)
{
	volatile uint32_t *gcr =
		(volatile uint32_t *)(((uint32_t *)(DT_REG_ADDR(DT_NODELABEL(gpioa)))) + pin_num);

	*gcr &= ~(GPIO_GCR_MFCTRL_Msk | GPIO_GCR_DIR_Msk);
	*gcr |= BIT(GPIO_GCR_INTCTRL_Pos) | GPIO_GCR_INTSTS_Msk | GPIO_GCR_INTEN_Msk |
		GPIO_GCR_INDETEN_Msk;
}

/**
 * @brief Get the SoC-wide pin number of a GPIO pin.
 *
 * The returned number, counting from pin 0 of port A, is also the interrupt
 * line assigned to the pin.
 *
 * @param gpio GPIO pin specification (port and pin)
 *
 * @return SoC-wide pin number
 */
int gpio_rts5912_get_pin_num(const struct gpio_dt_spec *gpio);

/**
 * @brief Get the base address of the per-pin control (GCR) registers of a
 * pin's GPIO port.
 *
 * @param gpio GPIO pin specification (port and pin)
 *
 * @return Address of the first per-pin control register of the port
 */
uint32_t *gpio_rts5912_get_port_address(const struct gpio_dt_spec *gpio);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_GPIO_GPIO_RTS5912_H_ */
