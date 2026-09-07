/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_INPUT_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_INPUT_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Register the touch callback. */
int aa_input_init(void);

/** @brief Allow forwarding touch events to the phone. */
void aa_input_link_up(void);

/** @brief Stop forwarding touch events. */
void aa_input_link_down(void);

/** @brief Handle a message received on the input channel. */
void aa_input_handle(uint16_t msg_id, const uint8_t *body, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_INPUT_H_ */
