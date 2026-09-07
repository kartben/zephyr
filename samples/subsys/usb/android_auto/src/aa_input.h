/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_INPUT_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_INPUT_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Reset the input channel state for a new link. */
void aa_input_link_up(void);

/** @brief Release any pressed pointer, the link is gone. */
void aa_input_link_down(void);

/** @brief Tell the head unit which key codes the sample handles. */
int aa_input_bind(void);

/** @brief Handle a message received on the input channel. */
void aa_input_handle(uint16_t msg_id, const uint8_t *body, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_INPUT_H_ */
