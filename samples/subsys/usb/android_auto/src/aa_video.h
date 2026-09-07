/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_VIDEO_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_VIDEO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Start the video thread. */
int aa_video_init(void);

/** @brief Reset the video channel state for a new link. */
void aa_video_link_up(void);

/** @brief Stop streaming, the link is gone. */
void aa_video_link_down(void);

/** @brief Send the setup request once the video channel is open. */
int aa_video_setup(void);

/** @brief Handle a message received on the video channel. */
void aa_video_handle(uint16_t msg_id, const uint8_t *body, size_t len);

/** @brief True while frames are being sent to the head unit. */
bool aa_video_is_streaming(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_VIDEO_H_ */
