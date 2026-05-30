/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mfd_interface_m5pm1
 * @brief Header file for the M5Stack M5PM1 MFD driver.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mfd_interface_m5pm1 MFD M5PM1 Interface
 * @ingroup mfd_interfaces
 * @brief M5Stack M5PM1 power management companion MCU interface.
 *
 * The M5PM1 is a small companion microcontroller used on M5Stack boards to provide power management
 * (battery charging, switchable rails), a small bank of GPIOs and an on-chip ADC. This MFD driver
 * owns the I2C transport and serializes register access; sibling GPIO, ADC and regulator drivers
 * implement their domain-specific functionality on top of these primitives.
 * @{
 */

/** Number of GPIO-capable pins exposed by the M5PM1. */
#define M5PM1_GPIO_PIN_COUNT 5U

/**
 * @brief M5PM1 GPIO pin function selector.
 *
 * The SPECIAL function maps to a fixed per-pin peripheral:
 * - pin 0: NeoPixel LED enable
 * - pin 1: ADC1
 * - pin 2: ADC2
 * - pin 3: PWM0
 * - pin 4: PWM1
 *
 * WAKE mode is only supported on pins 0, 2, 3, and 4. Additionally, the PMIC
 * only allows one active WAKE source from the pin pairs (0, 2) and (3, 4).
 */
enum m5pm1_gpio_func {
	M5PM1_GPIO_FUNC_GPIO = 0U,
	M5PM1_GPIO_FUNC_IRQ = 1U,
	M5PM1_GPIO_FUNC_WAKE = 2U,
	M5PM1_GPIO_FUNC_SPECIAL = 3U,
};

/**
 * @brief Read a single 8-bit M5PM1 register.
 *
 * @param dev M5PM1 MFD device.
 * @param reg Register address.
 * @param[out] val Pointer that receives the register value.
 *
 * @retval 0 On success.
 * @retval -errno On I2C transfer error (see i2c_reg_read_byte_dt()).
 */
int mfd_m5pm1_read_reg(const struct device *dev, uint8_t reg, uint8_t *val);

/**
 * @brief Write a single 8-bit M5PM1 register.
 *
 * @param dev M5PM1 MFD device.
 * @param reg Register address.
 * @param val Value to write.
 *
 * @retval 0 On success.
 * @retval -errno On I2C transfer error (see i2c_reg_write_byte_dt()).
 */
int mfd_m5pm1_write_reg(const struct device *dev, uint8_t reg, uint8_t val);

/**
 * @brief Read-modify-write selected bits of an M5PM1 register.
 *
 * Performs an atomic (mutex-protected) read-modify-write of @p reg, replacing the bits selected by
 * @p mask with the corresponding bits from @p val.
 *
 * @param dev M5PM1 MFD device.
 * @param reg Register address.
 * @param mask Bitmask of bits to update.
 * @param val New value for the bits selected by @p mask (other bits ignored).
 *
 * @retval 0 On success.
 * @retval -errno On I2C transfer error (see i2c_reg_update_byte_dt()).
 */
int mfd_m5pm1_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val);

/**
 * @brief Read multiple consecutive M5PM1 registers in a single I2C transaction.
 *
 * Uses a single repeated-start I2C transfer so the device sees the read as atomic, which matters
 * for register pairs latched on the low-byte read (e.g. ADC sample registers).
 *
 * @param dev M5PM1 MFD device.
 * @param reg Address of the first register to read.
 * @param[out] buf Buffer that receives @p len consecutive register values.
 * @param len Number of bytes to read.
 *
 * @retval 0 On success.
 * @retval -errno On I2C transfer error (see i2c_burst_read_dt()).
 */
int mfd_m5pm1_burst_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len);

/**
 * @brief Select the hardware function for an M5PM1 pin.
 *
 * This helper validates the documented PM1 pin-function matrix before
 * programming the GPIO function registers. Unsupported function selections are
 * rejected even when the corresponding Zephyr child driver is not implemented
 * yet.
 *
 * @param dev M5PM1 MFD device.
 * @param pin GPIO pin number in the range 0..4.
 * @param func Function to select for @p pin.
 *
 * @retval 0 On success.
 * @retval -EINVAL If @p pin is out of range.
 * @retval -ENOTSUP If @p func is not available on @p pin.
 * @retval -EBUSY If @p func is WAKE and the mutually-exclusive wake pin is
 * already configured for WAKE.
 * @retval -errno On I2C transfer error.
 */
int mfd_m5pm1_set_gpio_function(const struct device *dev, uint8_t pin,
				enum m5pm1_gpio_func func);

/**
 * @brief Configure the PM1 wake function for a pin.
 *
 * This programs the GPIO wake enable/edge registers and, when enabling wake,
 * also switches the pin function to @ref M5PM1_GPIO_FUNC_WAKE.
 *
 * @param dev M5PM1 MFD device.
 * @param pin GPIO pin number in the range 0..4.
 * @param enable True to enable wake on @p pin, false to disable it.
 * @param rising_edge True for rising-edge wake, false for falling-edge wake.
 *
 * @retval 0 On success.
 * @retval -EINVAL If @p pin is out of range.
 * @retval -ENOTSUP If wake is not available on @p pin.
 * @retval -EBUSY If enabling wake conflicts with another WAKE pin.
 * @retval -errno On I2C transfer error.
 */
int mfd_m5pm1_configure_gpio_wake(const struct device *dev, uint8_t pin, bool enable,
				  bool rising_edge);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_M5PM1_H_ */
