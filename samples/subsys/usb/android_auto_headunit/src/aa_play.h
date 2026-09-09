/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_PLAY_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_PLAY_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Prepare the codec and the output stream. */
int aa_play_init(void);

/**
 * @brief Hand one audio channel's samples over for playing.
 *
 * @param type One of AA_AUDIO_TYPE_GUIDANCE, _SYSTEM or _MEDIA.
 * @param pcm Little endian signed 16 bit samples at the channel's own rate.
 * @param len Size of @p pcm in bytes.
 */
void aa_play_submit(int32_t type, const uint8_t *pcm, size_t len);

/** @brief Drop what is queued for one channel, which has stopped. */
void aa_play_flush(int32_t type);

/** @brief Silence everything, the phone has gone. */
void aa_play_link_down(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_PLAY_H_ */
