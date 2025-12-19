/*
 * Copyright (c) 2025 Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file escp_printer.h
 * @brief Public API for Epson ESC/P printers
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ESCP_PRINTER_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ESCP_PRINTER_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Epson ESC/P printer driver APIs
 * @defgroup escp_printer Epson ESC/P printer
 * @ingroup misc_interfaces
 * @{
 */

/**
 * @brief Initialize the printer
 *
 * Sends the ESC @ command to initialize the printer.
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_init(const struct device *dev);

/**
 * @brief Print text to the printer
 *
 * @param dev Pointer to device structure for driver instance.
 * @param data The ASCII text to print
 * @param size The length of the text in bytes
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_print(const struct device *dev, const char *data, uint32_t size);

/**
 * @brief Print text followed by a line feed
 *
 * @param dev Pointer to device structure for driver instance.
 * @param data The ASCII text to print
 * @param size The length of the text in bytes
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_println(const struct device *dev, const char *data, uint32_t size);

/**
 * @brief Perform a line feed
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_line_feed(const struct device *dev);

/**
 * @brief Perform a form feed (eject page)
 *
 * @param dev Pointer to device structure for driver instance.
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_form_feed(const struct device *dev);

/**
 * @brief Set bold printing mode
 *
 * @param dev Pointer to device structure for driver instance.
 * @param enable true to enable bold, false to disable
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_set_bold(const struct device *dev, bool enable);

/**
 * @brief Set italic printing mode
 *
 * @param dev Pointer to device structure for driver instance.
 * @param enable true to enable italic, false to disable
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_set_italic(const struct device *dev, bool enable);

/**
 * @brief Set underline printing mode
 *
 * @param dev Pointer to device structure for driver instance.
 * @param enable true to enable underline, false to disable
 *
 * @retval 0 on success
 * @retval -errno on failure
 */
int escp_printer_set_underline(const struct device *dev, bool enable);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ESCP_PRINTER_H_ */
