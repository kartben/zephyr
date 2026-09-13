/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_CONTROL_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_CONTROL_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Send the initial version request to the phone. */
int aa_control_send_version_request(void);

/** @brief Handle a message received on the control channel. */
void aa_control_handle(uint16_t msg_id, const uint8_t *body, size_t len);

/** @brief Handle a channel open request received on a data channel. */
void aa_control_channel_open(uint8_t channel, const uint8_t *body, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_CONTROL_H_ */
