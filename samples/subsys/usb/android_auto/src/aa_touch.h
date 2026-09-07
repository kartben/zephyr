/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TOUCH_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TOUCH_H_

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Report a touch event received from the head unit.
 *
 * @param x Horizontal position in stream pixels.
 * @param y Vertical position in stream pixels.
 * @param pressed true for press and drag, false for release.
 *
 * @return 0 on success, negative errno from the input subsystem otherwise.
 */
int aa_touch_report(uint16_t x, uint16_t y, bool pressed);

/**
 * @brief Report a key event received from the head unit.
 *
 * @param code Zephyr input key code (INPUT_KEY_*).
 * @param pressed true when pressed, false when released.
 *
 * @return 0 on success, negative errno from the input subsystem otherwise.
 */
int aa_touch_report_key(uint16_t code, bool pressed);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_TOUCH_H_ */
