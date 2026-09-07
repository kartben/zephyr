/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_DISPLAY_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_DISPLAY_H_

#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

/* Stream geometry, shared by the display, the encoder and the protocol */
#define AA_VIDEO_WIDTH  CONFIG_SAMPLE_AA_VIDEO_WIDTH
#define AA_VIDEO_HEIGHT CONFIG_SAMPLE_AA_VIDEO_HEIGHT
#define AA_MBS_X        (AA_VIDEO_WIDTH / 16)
#define AA_MBS_Y        (AA_VIDEO_HEIGHT / 16)
#define AA_MBS          (AA_MBS_X * AA_MBS_Y)

struct aa_display_geometry {
	/** Framebuffer (LVGL) resolution */
	uint16_t lv_width;
	uint16_t lv_height;
	/** Stream resolution */
	uint16_t width;
	uint16_t height;
	/** Stream pixels per framebuffer pixel, 1 or 2 */
	uint8_t scale;
	/** Framebuffer bytes per pixel: 2 for RGB565, 1 for L8 */
	uint8_t bpp;
	uint16_t mbs_x;
	uint16_t mbs_y;
};

/** @brief Geometry of the virtual display. */
const struct aa_display_geometry *aa_display_geometry(void);

/** @brief Framebuffer holding the last rendered LVGL screen (row-major). */
const uint8_t *aa_display_framebuffer(void);

/**
 * @brief Dirty macroblock bitmap in stream macroblock raster order.
 *
 * Bits are set by the display driver after the framebuffer was updated and
 * cleared by the encoder when it captures a macroblock.
 */
atomic_t *aa_display_dirty(void);

/**
 * @brief Wait until an LVGL refresh cycle changed the framebuffer.
 *
 * @retval 0 A completed refresh left dirty macroblocks.
 * @retval -EAGAIN Timeout.
 */
int aa_display_wait_frame(k_timeout_t timeout);

/** @brief Number of dirty macroblocks. */
uint32_t aa_display_dirty_count(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_DISPLAY_H_ */
