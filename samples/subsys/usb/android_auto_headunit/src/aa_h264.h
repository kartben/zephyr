/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Set up the baseline decoder.
 *
 * @param fb     Framebuffer the pictures are written to, RGB565.
 * @param width  Framebuffer width in pixels.
 * @param height Framebuffer height in pixels.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The decoder could not be allocated.
 */
int aa_h264_init(uint16_t *fb, uint16_t width, uint16_t height);

/**
 * @brief Discard the decoder's state and start a new stream.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The decoder could not be allocated.
 */
int aa_h264_reset(void);

/**
 * @brief Decode one access unit.
 *
 * @retval 1 A picture was written to the framebuffer.
 * @retval 0 The access unit carried no complete picture.
 * @retval -EINVAL The stream could not be decoded.
 */
int aa_h264_decode_au(const uint8_t *au, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_ */
