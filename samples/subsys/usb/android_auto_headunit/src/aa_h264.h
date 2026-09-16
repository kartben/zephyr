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
 * Decoded pictures are handed to the screen, which composes them with
 * whatever else the display is showing.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The decoder could not be allocated.
 */
int aa_h264_init(void);

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
 * @retval 1 A picture was decoded and shown.
 * @retval 0 The access unit carried no complete picture.
 * @retval -EINVAL The stream could not be decoded.
 */
int aa_h264_decode_au(const uint8_t *au, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_ */
