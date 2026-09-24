/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup mfd_interface_m5ioe1
 * @brief Header file for the M5Stack M5IOE1 MFD driver.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_M5IOE1_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_M5IOE1_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup mfd_interface_m5ioe1 MFD M5IOE1 Interface
 * @ingroup mfd_interfaces
 * @brief M5Stack M5IOE1 I/O expander interface.
 * @since 4.5
 * @version 0.1.0
 *
 * The M5IOE1 is a PY32 microcontroller running M5Stack firmware that exposes 14 GPIOs, an ADC, PWM
 * outputs and a WS2812-style LED driver over I2C. This MFD driver owns the I2C transport and
 * serializes register access; child drivers implement their domain-specific functionality on top
 * of these primitives.
 *
 * Per-pin registers come in pairs: the register at the given address holds IO1..IO8 in bits 0..7
 * and the next register holds IO9..IO14 in bits 0..5. The 16-bit accessors below treat such a pair
 * as a single little-endian value, so bit @c n corresponds to IO<n+1>.
 * @{
 */

/**
 * @brief Read multiple consecutive M5IOE1 registers in a single I2C transaction.
 *
 * @important Burst access must not cross the 0x00-0x2f, 0x30-0x6f and 0x70-0x8f register blocks.
 *
 * @param dev M5IOE1 MFD device.
 * @param reg Address of the first register to read.
 * @param[out] buf Buffer that receives @p len consecutive register values.
 * @param len Number of bytes to read.
 *
 * @return 0 on success, negative errno value from i2c_burst_read_dt() on failure.
 */
int mfd_m5ioe1_burst_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len);

/**
 * @brief Write multiple consecutive M5IOE1 registers in a single I2C transaction.
 *
 * @important Burst access must not cross the 0x00-0x2f, 0x30-0x6f and 0x70-0x8f register blocks.
 *
 * @param dev M5IOE1 MFD device.
 * @param reg Address of the first register to write.
 * @param buf Buffer holding @p len consecutive register values.
 * @param len Number of bytes to write.
 *
 * @return 0 on success, negative errno value from i2c_burst_write_dt() on failure.
 */
int mfd_m5ioe1_burst_write(const struct device *dev, uint8_t reg, const uint8_t *buf, size_t len);

/**
 * @brief Read a per-pin register pair.
 *
 * @param dev M5IOE1 MFD device.
 * @param reg Address of the low register of the pair.
 * @param[out] val Pointer that receives the 16-bit value.
 *
 * @return 0 on success, negative errno value from the I2C transfer on failure.
 */
int mfd_m5ioe1_read_reg16(const struct device *dev, uint8_t reg, uint16_t *val);

/**
 * @brief Read-modify-write selected bits of a per-pin register pair.
 *
 * The read and the write are performed under the MFD lock, so the update is atomic with respect
 * to the other mfd_m5ioe1_*() accessors.
 *
 * @param dev M5IOE1 MFD device.
 * @param reg Address of the low register of the pair.
 * @param mask Bits to update.
 * @param val New value for the bits selected by @p mask (other bits ignored).
 *
 * @return 0 on success, negative errno value from the I2C transfer on failure.
 */
int mfd_m5ioe1_update_reg16(const struct device *dev, uint8_t reg, uint16_t mask, uint16_t val);

/**
 * @brief Invert selected bits of a per-pin register pair.
 *
 * @param dev M5IOE1 MFD device.
 * @param reg Address of the low register of the pair.
 * @param mask Bits to invert.
 *
 * @return 0 on success, negative errno value from the I2C transfer on failure.
 */
int mfd_m5ioe1_toggle_reg16(const struct device *dev, uint8_t reg, uint16_t mask);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_M5IOE1_H_ */
