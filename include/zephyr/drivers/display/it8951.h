/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup display_interface
 * @brief Public API for the IT8951 e-paper timing controller
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_IT8951_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_IT8951_H_

/**
 * @brief IT8951 display controller device-specific API extension
 * @defgroup it8951_display_interface IT8951 display controller
 * @ingroup display_interface
 * @{
 */

#include <stdint.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief IT8951 native 4bpp grayscale pixel format.
 *
 * 16 grey levels, 2 pixels per byte. The high nibble holds the left
 * pixel; values 0x0..0xF map to black..white. This is the native wire
 * format of the IT8951, so writes using PIXEL_FORMAT_L_4 buffers skip
 * per-line repacking.
 */
#define PIXEL_FORMAT_L_4 (PIXEL_FORMAT_PRIV_START << 0)

/**
 * @name IT8951 waveform modes
 * @{
 */
#define IT8951_WAVEFORM_INIT 0U /**< Full clear / panel initialization. */
#define IT8951_WAVEFORM_DU   1U /**< Direct update, monochrome. */
#define IT8951_WAVEFORM_GC16 2U /**< 16 grey levels, full refresh. */
#define IT8951_WAVEFORM_GL16 3U /**< 16 grey levels, ghost reduction. */
#define IT8951_WAVEFORM_A2   4U /**< Fast binary partial refresh. */
/** @} */

/**
 * @brief Set the default waveform mode used by display_write().
 *
 * @param dev  IT8951 device.
 * @param mode One of the IT8951_WAVEFORM_* constants.
 *
 * @retval 0 on success.
 * @retval -EINVAL on invalid mode.
 */
int it8951_set_waveform_mode(const struct device *dev, uint8_t mode);

/**
 * @brief Trigger a panel refresh of an arbitrary region with a chosen waveform.
 *
 * Pixel data already written to the controller's image buffer (via
 * display_write()) is displayed on the panel using @p waveform_mode.
 *
 * @param dev           IT8951 device.
 * @param x             Region X origin in pixels.
 * @param y             Region Y origin in pixels.
 * @param w             Region width in pixels.
 * @param h             Region height in pixels.
 * @param waveform_mode One of the IT8951_WAVEFORM_* constants.
 *
 * @retval 0 on success.
 * @retval -EINVAL when the region is outside the panel.
 * @retval -EIO on transport failure.
 */
int it8951_refresh(const struct device *dev, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
		   uint8_t waveform_mode);

/**
 * @brief Clear the panel using the given waveform.
 *
 * Loads a white image covering the full panel and triggers a refresh.
 *
 * @param dev           IT8951 device.
 * @param waveform_mode One of the IT8951_WAVEFORM_* constants.
 *
 * @retval 0 on success.
 * @retval -EIO on transport failure.
 */
int it8951_clear(const struct device *dev, uint8_t waveform_mode);

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_IT8951_H_ */
