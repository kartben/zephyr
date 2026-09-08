/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_STM32_LTDC_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_STM32_LTDC_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>

/**
 * @brief STM32 LTDC display controller
 * @defgroup stm32_ltdc STM32 LTDC
 * @ingroup display_interface
 * @{
 */

/**
 * @brief Display a packed YUV 4:2:2 picture.
 *
 * The buffer contains tightly packed Y0, U, Y1, V bytes at the display's
 * configured resolution, with ITU-R BT.601 limited-range samples. The LTDC
 * converts these samples to RGB during scanout. STM32N6 planar YUV420 modes
 * cannot be used because of silicon erratum ES0620, section 2.7.1.
 *
 * This call switches the controller to YUV mode. Do not mix this API with
 * RGB display operations. Calls must be serialized. Frame swaps complete
 * at VSync before returning, so the previous buffer can then be reused.
 * The current buffer must remain valid and unmodified until replaced.
 * Requires CONFIG_STM32_LTDC_YUV.
 *
 * @param dev Display device.
 * @param buf Packed YUYV frame.
 * @param len Buffer size in bytes.
 *
 * @retval 0 Success.
 * @retval -EINVAL Buffer is missing, too small, or the display width is odd.
 * @retval -EIO The controller rejected the configuration.
 */
int stm32_ltdc_set_yuyv_frame(const struct device *dev, const uint8_t *buf, size_t len);

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_STM32_LTDC_H_ */
