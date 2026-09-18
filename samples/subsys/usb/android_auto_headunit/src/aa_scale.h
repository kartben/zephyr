/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_

#include <stdint.h>

#include "aa_layout.h"

/*
 * Writers that put one picture into a rectangle of the display surface. The
 * surface is packed YUV 4:2:2, which the display controller converts during
 * scanout, or RGB565 for a display that cannot, or ARGB8888 for one that scans
 * nothing narrower. Rectangles must start on an even column and be an even
 * number of pixels wide, because a packed YUV pixel pair shares its chroma
 * samples.
 *
 * Only the two ratios the sample settles at are worth writing quickly: the
 * whole display, and a quarter of it with every second sample dropped. The
 * ratios in between are only on screen while the two are being animated
 * between, and take a general nearest neighbour path.
 */

/**
 * @brief Write a planar YUV420 picture into a rectangle of a packed YUYV surface.
 *
 * @param dst   Start of the surface.
 * @param pitch Surface width in pixels.
 * @param r     Rectangle of the surface to fill.
 * @param pic   Planar YUV420 picture, luma plane first.
 * @param w,h   Picture size in pixels, both even.
 */
void aa_scale_i420_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
			uint16_t w, uint16_t h);

/**
 * @brief Write a planar YUV420 picture into a rectangle of an RGB565 surface.
 *
 * The BT.601 limited range samples the phone sends are converted on the way.
 *
 * @param dst   Start of the surface.
 * @param pitch Surface width in pixels.
 * @param r     Rectangle of the surface to fill.
 * @param pic   Planar YUV420 picture, luma plane first.
 * @param w,h   Picture size in pixels, both even.
 */
void aa_scale_i420_rgb565(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint8_t *pic, uint16_t w, uint16_t h);

/**
 * @brief Write a planar YUV420 picture into a rectangle of an ARGB8888 surface.
 *
 * As aa_scale_i420_rgb565(), for a display whose narrowest pixel is a word.
 * Every pixel is written fully opaque.
 *
 * @param dst   Start of the surface.
 * @param pitch Surface width in pixels.
 * @param r     Rectangle of the surface to fill.
 * @param pic   Planar YUV420 picture, luma plane first.
 * @param w,h   Picture size in pixels, both even.
 */
void aa_scale_i420_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			    const uint8_t *pic, uint16_t w, uint16_t h);

/** @brief Blacken a rectangle of a packed YUYV surface. */
void aa_scale_fill_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r);

/** @brief Blacken a rectangle of an RGB565 surface. */
void aa_scale_fill_rgb565(uint16_t *dst, uint16_t pitch, const struct aa_rect *r);

/** @brief Blacken a rectangle of an ARGB8888 surface. */
void aa_scale_fill_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r);

/**
 * @brief Copy RGB565 pixels into a rectangle of a packed YUYV surface.
 *
 * @param dst       Start of the surface.
 * @param pitch     Surface width in pixels.
 * @param r         Rectangle of the surface to fill.
 * @param src       First source pixel, which lands at the corner of @p r.
 * @param src_pitch Source width in pixels.
 */
void aa_scale_rgb565_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint16_t *src, uint16_t src_pitch);

/** @brief Copy RGB565 pixels into a rectangle of an RGB565 surface. */
void aa_scale_rgb565_copy(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint16_t *src, uint16_t src_pitch);

/** @brief Copy RGB565 pixels into a rectangle of an ARGB8888 surface. */
void aa_scale_rgb565_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			      const uint16_t *src, uint16_t src_pitch);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCALE_H_ */
