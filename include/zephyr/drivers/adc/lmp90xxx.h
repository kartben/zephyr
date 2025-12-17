/*
 * Copyright (c) 2019 Vestas Wind Systems A/S
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup adc_lmp90xxx
 * @brief API for LMP90xxx ADC GPIO functionality.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_

/**
 * @brief LMP90xxx ADC GPIO API
 * @defgroup adc_lmp90xxx LMP90xxx ADC GPIO
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/** LMP90xxx supports GPIO D0..D6 */
#define LMP90XXX_GPIO_MAX 6

/**
 * @brief Configure a GPIO pin as output
 *
 * Configures a GPIO pin on the LMP90xxx ADC as an output.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param pin GPIO pin number to configure (0-6).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_set_output(const struct device *dev, uint8_t pin);

/**
 * @brief Configure a GPIO pin as input
 *
 * Configures a GPIO pin on the LMP90xxx ADC as an input.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param pin GPIO pin number to configure (0-6).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_set_input(const struct device *dev, uint8_t pin);

/**
 * @brief Set the value of a GPIO pin
 *
 * Sets the output value of a GPIO pin on the LMP90xxx ADC.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param pin   GPIO pin number (0-6).
 * @param value Value to set (true for high, false for low).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_set_pin_value(const struct device *dev, uint8_t pin,
				bool value);

/**
 * @brief Get the value of a GPIO pin
 *
 * Reads the current value of a GPIO pin on the LMP90xxx ADC.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param pin   GPIO pin number (0-6).
 * @param value Pointer to store the pin value (true for high, false for low).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_get_pin_value(const struct device *dev, uint8_t pin,
				bool *value);

/**
 * @brief Get raw value of GPIO port
 *
 * Reads the raw value of all GPIO pins on the LMP90xxx ADC port.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param value Pointer to store the raw port value.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_port_get_raw(const struct device *dev,
			       gpio_port_value_t *value);

/**
 * @brief Set masked raw value of GPIO port
 *
 * Sets the raw value of selected GPIO pins on the LMP90xxx ADC port using a mask.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param mask  Bit mask of pins to modify.
 * @param value Value to set for the masked pins.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_port_set_masked_raw(const struct device *dev,
				      gpio_port_pins_t mask,
				      gpio_port_value_t value);

/**
 * @brief Set raw bits of GPIO port
 *
 * Sets (turns on) selected GPIO pins on the LMP90xxx ADC port.
 *
 * @param dev  Pointer to the device structure for the driver instance.
 * @param pins Bit mask of pins to set.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_port_set_bits_raw(const struct device *dev,
				    gpio_port_pins_t pins);

/**
 * @brief Clear raw bits of GPIO port
 *
 * Clears (turns off) selected GPIO pins on the LMP90xxx ADC port.
 *
 * @param dev  Pointer to the device structure for the driver instance.
 * @param pins Bit mask of pins to clear.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_port_clear_bits_raw(const struct device *dev,
				      gpio_port_pins_t pins);

/**
 * @brief Toggle GPIO port bits
 *
 * Toggles the state of selected GPIO pins on the LMP90xxx ADC port.
 *
 * @param dev  Pointer to the device structure for the driver instance.
 * @param pins Bit mask of pins to toggle.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int lmp90xxx_gpio_port_toggle_bits(const struct device *dev,
				   gpio_port_pins_t pins);

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_LMP90XXX_H_ */
