/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MIC_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MIC_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Prepare the microphone. */
int aa_mic_init(void);

/** @brief Handle a message on the microphone channel. */
void aa_mic_handle(uint16_t msg_id, const uint8_t *body, size_t len);

/** @brief Stop capturing when the phone goes away. */
void aa_mic_link_down(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MIC_H_ */
