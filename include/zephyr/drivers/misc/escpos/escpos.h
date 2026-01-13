/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
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
 * @defgroup escpos_interface ESC/POS thermal printer driver API
 * @ingroup misc_interfaces
 * @{
 */

/**
 * @brief Text alignment options
 */
enum escpos_align {
	ESCPOS_ALIGN_LEFT = 0,
	ESCPOS_ALIGN_CENTER = 1,
	ESCPOS_ALIGN_RIGHT = 2,
};

/**
 * @brief Font size options
 */
enum escpos_font_size {
	ESCPOS_FONT_SIZE_NORMAL = 0,
	ESCPOS_FONT_SIZE_DOUBLE_HEIGHT = 1,
	ESCPOS_FONT_SIZE_DOUBLE_WIDTH = 2,
	ESCPOS_FONT_SIZE_DOUBLE_BOTH = 3,
};

/**
 * @brief Initialize the ESC/POS printer
 *
 * @param dev Pointer to device structure
 * @return 0 on success, negative errno code on failure
 */
int escpos_init(const struct device *dev);

/**
 * @brief Print text to the thermal printer
 *
 * @param dev Pointer to device structure
 * @param data Text data to print
 * @param len Length of text data
 * @return 0 on success, negative errno code on failure
 */
int escpos_print(const struct device *dev, const uint8_t *data, size_t len);

/**
 * @brief Print text string to the thermal printer
 *
 * @param dev Pointer to device structure
 * @param str Null-terminated string to print
 * @return 0 on success, negative errno code on failure
 */
int escpos_print_str(const struct device *dev, const char *str);

/**
 * @brief Feed paper by specified number of lines
 *
 * @param dev Pointer to device structure
 * @param lines Number of lines to feed (0-255)
 * @return 0 on success, negative errno code on failure
 */
int escpos_feed_lines(const struct device *dev, uint8_t lines);

/**
 * @brief Cut paper
 *
 * @param dev Pointer to device structure
 * @param partial True for partial cut, false for full cut
 * @return 0 on success, negative errno code on failure
 */
int escpos_cut_paper(const struct device *dev, bool partial);

/**
 * @brief Set text alignment
 *
 * @param dev Pointer to device structure
 * @param align Alignment option
 * @return 0 on success, negative errno code on failure
 */
int escpos_set_align(const struct device *dev, enum escpos_align align);

/**
 * @brief Set font size
 *
 * @param dev Pointer to device structure
 * @param size Font size option
 * @return 0 on success, negative errno code on failure
 */
int escpos_set_font_size(const struct device *dev, enum escpos_font_size size);

/**
 * @brief Enable or disable bold text
 *
 * @param dev Pointer to device structure
 * @param enable True to enable bold, false to disable
 * @return 0 on success, negative errno code on failure
 */
int escpos_set_bold(const struct device *dev, bool enable);

/**
 * @brief Enable or disable underline text
 *
 * @param dev Pointer to device structure
 * @param enable True to enable underline, false to disable
 * @return 0 on success, negative errno code on failure
 */
int escpos_set_underline(const struct device *dev, bool enable);

/**
 * @brief Reset printer to default settings
 *
 * @param dev Pointer to device structure
 * @return 0 on success, negative errno code on failure
 */
int escpos_reset(const struct device *dev);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MISC_ESCPOS_ESCPOS_H_ */
