/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_NAV_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_NAV_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Prepare the display the next turn is shown on. */
int aa_nav_init(void);

/** @brief Handle a message on the navigation status channel. */
void aa_nav_handle(uint16_t msg_id, const uint8_t *body, size_t len);

/** @brief Clear the display, the phone has gone. */
void aa_nav_link_down(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_NAV_H_ */
