/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_UI_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_UI_H_

#include <stdbool.h>
#include <stdint.h>

enum aa_ui_link_state {
	AA_UI_LINK_DOWN,
	AA_UI_LINK_CONNECTED,
	AA_UI_LINK_SECURE,
	AA_UI_LINK_STREAMING,
};

/**
 * @brief Build the demo screen. Must be called with the LVGL lock held.
 *
 * @retval 0 Success.
 */
int aa_ui_init(void);

/** @brief Update the link state label (callable from any thread). */
void aa_ui_set_link_state(enum aa_ui_link_state state);

/** @brief Switch between the light and dark theme (callable from any thread). */
void aa_ui_set_night(bool night);

/**
 * @brief Centre of the counter button in framebuffer pixels, for the self-test.
 *
 * Must be called with the LVGL lock held.
 *
 * @retval 0 Success.
 * @retval -ENOENT The sample screen is not shown.
 */
int aa_ui_get_tap_target(uint16_t *x, uint16_t *y);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_UI_H_ */
