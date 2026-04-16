/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup display_interface
 * @brief UC8159C multi-level / color EPD (packed framebuffer and palette indices)
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_DISPLAY_UC8159C_H_
#define ZEPHYR_INCLUDE_DRIVERS_DISPLAY_UC8159C_H_

/**
 * @brief UC8159C display controller device-specific API helpers
 * @defgroup uc8159c_display_interface UC8159C display controller
 * @ingroup display_interface
 * @{
 */

#include <zephyr/drivers/display.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UC8159C packed pixel format (private display format)
 *
 * Two pixels per byte on DTM1. Each pixel is one **nibble** (4 bits) selecting
 * an index into the panel LUT (indices 0–7 for Spectra 6 / 8-color panels).
 * This matches common UC8159 reference code (e.g. Pimoroni: high nibble first
 * pixel, low nibble second). Do **not** pack two 3-bit values into six bits;
 * that misaligns the shift register and yields pale or wrong colors.
 */
#define PIXEL_FORMAT_UC8159C (PIXEL_FORMAT_PRIV_START << 1)

/** Bits stored per logical pixel in the framebuffer (one nibble per pixel on wire) */
#define UC8159C_BITS_PER_PIXEL 4U

/** Pixels packed per byte for @ref PIXEL_FORMAT_UC8159C */
#define UC8159C_PIXELS_PER_BYTE 2U

/**
 * @name UC8159C KPIXEL[2:0] values (DTM1, see UC8159C datasheet)
 *
 * Each value selects a waveform / color pair from the panel LUT (OTP or
 * loaded registers). Actual on-screen colors depend on the panel.
 * @{
 */
#define UC8159C_KPIX_BW		0x1U /**< 001b table row */
#define UC8159C_KPIX_G1G2	0x2U /**< 010b */
#define UC8159C_KPIX_G2G1	0x3U /**< 011b */
#define UC8159C_KPIX_WB		0x4U /**< 100b */
#define UC8159C_KPIX_R0R3	0x5U /**< 101b */
#define UC8159C_KPIX_R1R2	0x6U /**< 110b */
#define UC8159C_KPIX_R2R3	0x7U /**< 111b */
/** @} */

/**
 * @name UC8159C DTM1 nibble values (Spectra 6 / multi-color)
 *
 * Each macro is the **4-bit value** written per pixel on DTM1 (two pixels
 * per byte via @ref UC8159C_PACK_PIXELS). For **GDEP073E01** and the same COG,
 * these match Good Display / Jean-Marc Zingg GxEPD2
 * ``GxEPD2_730c_GDEP073E01::_convert_to_native`` output (native wire nibbles).
 *
 * Waveshare Arduino ``EPD_7IN3F_*`` uses a **different** index→nibble map
 * (e.g. their ``GREEN`` is ``0x02``, which on GDEP073 is **yellow** on wire).
 * If colors look swapped or washed (green as pale yellow), the panel likely
 * expects this native map, not the Waveshare literal 0–5 order.
 *
 * They are **not** the datasheet @ref UC8159C_KPIX_* row labels.
 * @{
 */
#define UC8159C_PALETTE_BLACK	0U
#define UC8159C_PALETTE_WHITE	1U
#define UC8159C_PALETTE_YELLOW	2U
#define UC8159C_PALETTE_RED	3U
#define UC8159C_PALETTE_ORANGE	4U
#define UC8159C_PALETTE_BLUE	5U
#define UC8159C_PALETTE_GREEN	6U
#define UC8159C_PALETTE_CLEAN	7U
/** @} */

/**
 * @brief Pack two UC8159C LUT indices into one byte for DTM1 (two nibbles)
 *
 * @param pix_h First pixel (horizontal left): bits [7:4]
 * @param pix_l Second pixel: bits [3:0]
 */
#define UC8159C_PACK_PIXELS(pix_h, pix_l) \
	((uint8_t)((((pix_h) & 0xFU) << 4) | (((pix_l) & 0xFU))))

/** Framebuffer size in bytes for @ref PIXEL_FORMAT_UC8159C */
#define UC8159C_BUFFER_SIZE(width, height) (((width) * (height)) / UC8159C_PIXELS_PER_BYTE)

#ifdef __cplusplus
}
#endif

/** @} */

#endif /* ZEPHYR_INCLUDE_DRIVERS_DISPLAY_UC8159C_H_ */
