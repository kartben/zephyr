/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_

#include <stdint.h>

/*
 * Writers that put one decoded picture onto the display surface. The surface
 * is either packed YUV 4:2:2, which the display controller converts during
 * scanout, or RGB565 for a display that cannot.
 */

/**
 * @brief Interleave a planar YUV420 picture as packed YUYV.
 *
 * @param dst  Start of the surface, which is the size of the picture.
 * @param pic  Planar YUV420 picture, luma plane first.
 * @param w,h  Picture size in pixels, both even.
 */
void aa_scale_i420_yuyv(uint8_t *dst, const uint8_t *pic, uint16_t w, uint16_t h);

/**
 * @brief Convert a planar YUV420 picture to RGB565.
 *
 * The BT.601 limited range samples the phone sends are converted on the way,
 * and a picture larger than the surface is clipped to it.
 *
 * @param dst       Start of the surface.
 * @param dst_w,dst_h Surface size in pixels.
 * @param pic       Planar YUV420 picture, luma plane first.
 * @param w,h       Picture size in pixels, both even.
 */
void aa_scale_i420_rgb565(uint16_t *dst, uint16_t dst_w, uint16_t dst_h, const uint8_t *pic,
			  uint16_t w, uint16_t h);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_ */
