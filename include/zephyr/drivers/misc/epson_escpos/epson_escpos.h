/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for Epson ESC/POS thermal printer driver.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_EPSON_ESCPOS_EPSON_ESCPOS_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_EPSON_ESCPOS_EPSON_ESCPOS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Interfaces for Epson ESC/POS thermal printers.
 * @defgroup epson_escpos Epson ESC/POS thermal printer
 * @ingroup misc_interfaces
 * @{
 */

/** @brief Print justification options */
enum escpos_justify {
	ESCPOS_JUSTIFY_LEFT = 0,
	ESCPOS_JUSTIFY_CENTER = 1,
	ESCPOS_JUSTIFY_RIGHT = 2,
};

/**
 * @brief Initialize the printer
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_init_printer(const struct device *dev);

/**
 * @brief Print text
 *
 * @param dev Pointer to device structure for driver instance.
 * @param data Text to print.
 * @param len Length of text in bytes.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_print(const struct device *dev, const uint8_t *data, uint16_t len);

/**
 * @brief Print text followed by a line feed
 *
 * @param dev Pointer to device structure for driver instance.
 * @param data Text to print.
 * @param len Length of text in bytes.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_println(const struct device *dev, const uint8_t *data, uint16_t len);

/**
 * @brief Feed n lines
 *
 * @param dev Pointer to device structure for driver instance.
 * @param lines Number of lines to feed.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_feed(const struct device *dev, uint8_t lines);

/**
 * @brief Cut the paper (full cut)
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_cut(const struct device *dev);

/**
 * @brief Cut the paper (partial cut)
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_cut_partial(const struct device *dev);

/**
 * @brief Set text justification
 *
 * @param dev Pointer to device structure for driver instance.
 * @param justify Justification mode (left, center, right).
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_set_justify(const struct device *dev, enum escpos_justify justify);

/**
 * @brief Enable or disable bold text
 *
 * @param dev Pointer to device structure for driver instance.
 * @param enabled true to enable bold, false to disable.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_set_bold(const struct device *dev, bool enabled);

/**
 * @brief Enable or disable underline text
 *
 * @param dev Pointer to device structure for driver instance.
 * @param enabled true to enable underline, false to disable.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_set_underline(const struct device *dev, bool enabled);

/**
 * @brief Set text size
 *
 * @param dev Pointer to device structure for driver instance.
 * @param width Width multiplier (1-8).
 * @param height Height multiplier (1-8).
 *
 * @retval 0 If successful.
 * @retval -EINVAL If width or height is out of range.
 * @retval -errno Negative errno code on failure.
 */
int escpos_set_text_size(const struct device *dev, uint8_t width, uint8_t height);

/**
 * @brief Reset text formatting to default
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 If successful.
 * @retval -errno Negative errno code on failure.
 */
int escpos_reset_formatting(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_EPSON_ESCPOS_EPSON_ESCPOS_H_ */
