/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_VIDEO_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_VIDEO_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Initialize the decoder and the display. */
int aa_video_init(void);

/** @brief Reset the video state for a new link. */
void aa_video_link_up(void);

/** @brief Stop the video, the link is gone. */
void aa_video_link_down(void);

/** @brief Handle a message received on the video channel. */
void aa_video_handle(uint16_t msg_id, const uint8_t *body, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_VIDEO_H_ */
