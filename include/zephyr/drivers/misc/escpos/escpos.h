/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API for ESC/POS thermal printer driver
 * @ingroup escpos_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MISC_ESCPOS_ESCPOS_H_
#define ZEPHYR_INCLUDE_DRIVERS_MISC_ESCPOS_ESCPOS_H_

#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ESC/POS thermal printer driver API
 * @defgroup escpos_interface ESC/POS thermal printer driver
 * @ingroup misc_interfaces
 * @{
 */

/**
 * @brief Text alignment options
 */
enum escpos_align {
	ESCPOS_ALIGN_LEFT = 0,   /**< Left-aligned text */
	ESCPOS_ALIGN_CENTER = 1, /**< Center-aligned text */
	ESCPOS_ALIGN_RIGHT = 2,  /**< Right-aligned text */
};

/**
 * @brief Font size options
 */
enum escpos_font_size {
	ESCPOS_FONT_SIZE_NORMAL = 0,        /**< Normal font size */
	ESCPOS_FONT_SIZE_DOUBLE_HEIGHT = 1, /**< Double height text */
	ESCPOS_FONT_SIZE_DOUBLE_WIDTH = 2,  /**< Double width text */
	ESCPOS_FONT_SIZE_DOUBLE_BOTH = 3,   /**< Double height and width text */
};

/**
 * @brief Initialize the ESC/POS printer
 *
 * Resets the printer to default settings.
 *
 * @param[in] dev Pointer to device structure
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_init(const struct device *dev);

/**
 * @brief Print raw data to the thermal printer
 *
 * Sends raw byte data to the printer. Use escpos_print_str() for
 * null-terminated strings.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] data Byte array to print
 * @param[in] len Length of data array in bytes
 * @retval 0 Success
 * @retval -EINVAL Invalid parameters (NULL pointer or zero length)
 * @retval -ENODEV UART device not available
 */
int escpos_print(const struct device *dev, const uint8_t *data, size_t len);

/**
 * @brief Print text string to the thermal printer
 *
 * Convenience function for printing null-terminated strings.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] str Null-terminated string to print
 * @retval 0 Success
 * @retval -EINVAL Invalid parameters (NULL pointer)
 * @retval -ENODEV UART device not available
 */
int escpos_print_str(const struct device *dev, const char *str);

/**
 * @brief Feed paper by specified number of lines
 *
 * Advances the paper forward.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] lines Number of lines to feed (0-255)
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_feed_lines(const struct device *dev, uint8_t lines);

/**
 * @brief Cut paper
 *
 * Activates the paper cutter mechanism.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] partial True for partial cut, false for full cut
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_cut_paper(const struct device *dev, bool partial);

/**
 * @brief Set text alignment
 *
 * Controls horizontal alignment for subsequent text.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] align Alignment option (left, center, or right)
 * @retval 0 Success
 * @retval -EINVAL Invalid alignment value
 * @retval -ENODEV UART device not available
 */
int escpos_set_align(const struct device *dev, enum escpos_align align);

/**
 * @brief Set font size
 *
 * Controls the size of subsequent text.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] size Font size option
 * @retval 0 Success
 * @retval -EINVAL Invalid font size value
 * @retval -ENODEV UART device not available
 */
int escpos_set_font_size(const struct device *dev, enum escpos_font_size size);

/**
 * @brief Enable or disable bold text
 *
 * Controls bold/emphasized text for subsequent printing.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] enable True to enable bold, false to disable
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_set_bold(const struct device *dev, bool enable);

/**
 * @brief Enable or disable underline text
 *
 * Controls underlined text for subsequent printing.
 *
 * @param[in] dev Pointer to device structure
 * @param[in] enable True to enable underline, false to disable
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_set_underline(const struct device *dev, bool enable);

/**
 * @brief Reset printer to default settings
 *
 * Equivalent to calling escpos_init().
 *
 * @param[in] dev Pointer to device structure
 * @retval 0 Success
 * @retval -ENODEV UART device not available
 */
int escpos_reset(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ESCPOS_ESCPOS_H_ */
