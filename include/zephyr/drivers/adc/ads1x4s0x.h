/*
 * Copyright (c) 2023 SILA Embedded Solutions GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup adc_ads1x4s0x
 * @brief API for ADS1x4S0x ADC GPIO functionality.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1X4S0X_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1X4S0X_H_

/**
 * @brief ADS1x4S0x ADC GPIO API
 * @defgroup adc_ads1x4s0x ADS1x4S0x ADC GPIO
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

/**
 * @brief Configure a GPIO pin as output
 *
 * Configures a GPIO pin on the ADS1x4S0x ADC as an output with an initial value.
 *
 * @param dev           Pointer to the device structure for the driver instance.
 * @param pin           GPIO pin number to configure.
 * @param initial_value Initial output value for the pin.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_set_output(const struct device *dev, uint8_t pin, bool initial_value);

/**
 * @brief Configure a GPIO pin as input
 *
 * Configures a GPIO pin on the ADS1x4S0x ADC as an input.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param pin GPIO pin number to configure.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_set_input(const struct device *dev, uint8_t pin);

/**
 * @brief Deconfigure a GPIO pin
 *
 * Removes the configuration of a GPIO pin on the ADS1x4S0x ADC.
 *
 * @param dev Pointer to the device structure for the driver instance.
 * @param pin GPIO pin number to deconfigure.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_deconfigure(const struct device *dev, uint8_t pin);

/**
 * @brief Set the value of a GPIO pin
 *
 * Sets the output value of a GPIO pin on the ADS1x4S0x ADC.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param pin   GPIO pin number.
 * @param value Value to set (true for high, false for low).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_set_pin_value(const struct device *dev, uint8_t pin,
				bool value);

/**
 * @brief Get the value of a GPIO pin
 *
 * Reads the current value of a GPIO pin on the ADS1x4S0x ADC.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param pin   GPIO pin number.
 * @param value Pointer to store the pin value (true for high, false for low).
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_get_pin_value(const struct device *dev, uint8_t pin,
				bool *value);

/**
 * @brief Get raw value of GPIO port
 *
 * Reads the raw value of all GPIO pins on the ADS1x4S0x ADC port.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param value Pointer to store the raw port value.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_port_get_raw(const struct device *dev,
			       gpio_port_value_t *value);

/**
 * @brief Set masked raw value of GPIO port
 *
 * Sets the raw value of selected GPIO pins on the ADS1x4S0x ADC port using a mask.
 *
 * @param dev   Pointer to the device structure for the driver instance.
 * @param mask  Bit mask of pins to modify.
 * @param value Value to set for the masked pins.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_port_set_masked_raw(const struct device *dev,
				      gpio_port_pins_t mask,
				      gpio_port_value_t value);

/**
 * @brief Toggle GPIO port bits
 *
 * Toggles the state of selected GPIO pins on the ADS1x4S0x ADC port.
 *
 * @param dev  Pointer to the device structure for the driver instance.
 * @param pins Bit mask of pins to toggle.
 *
 * @retval 0 on success.
 * @retval Negative error code on failure.
 */
int ads1x4s0x_gpio_port_toggle_bits(const struct device *dev,
				   gpio_port_pins_t pins);

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1X4S0X_H_ */
