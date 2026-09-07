/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_YUV_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_YUV_H_

#include <stddef.h>
#include <stdint.h>

/*
 * Converters from framebuffer pixels to one I_PCM macroblock: 256 luma samples
 * in raster order followed by 64 Cb and 64 Cr samples (4:2:0). Colours follow
 * BT.601 with limited range, so every sample is in [16, 240].
 */

/** Convert a 16x16 RGB565 block (little-endian pixels), 2x2 chroma averaging. */
void yuv_mb_rgb565(const uint16_t *src, size_t stride_px, uint8_t *pcm);

/** Convert an 8x8 RGB565 block pixel-doubled to 16x16, one chroma sample per pixel. */
void yuv_mb_rgb565_x2(const uint16_t *src, size_t stride_px, uint8_t *pcm);

/** Convert a 16x16 8-bit grayscale block. */
void yuv_mb_l8(const uint8_t *src, size_t stride_px, uint8_t *pcm);

/** Convert an 8x8 8-bit grayscale block pixel-doubled to 16x16. */
void yuv_mb_l8_x2(const uint8_t *src, size_t stride_px, uint8_t *pcm);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_YUV_H_ */
