/*
 * Copyright (c) 2026 Benjamin Lemouzy
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup display_interface
 * @brief GDEP073E01 7.3" Spectra 6 e-paper (4bpp, two pixels per byte)
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_GDEP073E01_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_GDEP073E01_H_

#include <zephyr/drivers/display.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Same packed 4bpp encoding as other Zephyr multi-color e-paper drivers
 *        (high nibble = left pixel, low nibble = right pixel).
 */
#ifndef PIXEL_FORMAT_L_4
#define PIXEL_FORMAT_L_4 (PIXEL_FORMAT_PRIV_START << 0)
#endif

#define GDEP073E01_BITS_PER_PIXEL 4

/** @name Spectra 6 palette indices (Waveshare / Good Display reference) */
/** @{ */
#define GDEP073E01_COLOR_BLACK  0x00U
#define GDEP073E01_COLOR_WHITE  0x01U
#define GDEP073E01_COLOR_GREEN  0x02U
#define GDEP073E01_COLOR_BLUE   0x03U
#define GDEP073E01_COLOR_RED    0x04U
#define GDEP073E01_COLOR_YELLOW 0x05U
/** @} */

#define GDEP073E01_PACK_PIXELS(pixel1, pixel2) (((pixel1) << 4) | ((pixel2) & 0x0FU))

#define GDEP073E01_BUFFER_SIZE(width, height) (((width) * (height)) / 2U)

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_GDEP073E01_H_ */
