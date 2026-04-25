/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup display_interface
 * @brief Palette definitions for T133A01 full-color e-paper display
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_T133A01_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_T133A01_H_

/**
 * @defgroup t133a01_display_interface T133A01 display controller
 * @brief T133A01 full-color e-paper display controller device-specific API extension
 * @ingroup display_interface
 * @{
 */

#include <zephyr/drivers/display.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name T133A01 Color Definitions
 * @{
 */
#define T133A01_COLOR_BLACK  0x00 /**< Black color index */
#define T133A01_COLOR_WHITE  0x01 /**< White color index */
#define T133A01_COLOR_YELLOW 0x02 /**< Yellow color index */
#define T133A01_COLOR_RED    0x03 /**< Red color index */
#define T133A01_COLOR_BLUE   0x05 /**< Blue color index */
#define T133A01_COLOR_GREEN  0x06 /**< Green color index */
/** @} */

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_T133A01_H_ */
