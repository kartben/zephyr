/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_AUDIO_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_AUDIO_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Handle a message on one of the audio channels. */
void aa_audio_handle(uint8_t channel, uint16_t msg_id, const uint8_t *body, size_t len);

/** @brief True when the channel is one of the advertised audio channels. */
bool aa_audio_is_audio_channel(uint8_t channel);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_AUDIO_H_ */
