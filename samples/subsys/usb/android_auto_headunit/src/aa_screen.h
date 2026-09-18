/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCREEN_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCREEN_H_

#include <stdbool.h>
#include <stdint.h>

/*
 * The display surface, and the one place that hands it to the controller. The
 * phone's pictures and the head unit's own GUI are both drawn into it, from
 * different threads, so every producer goes through here.
 */

/** Buffers the surface is scanned out of, and so the buffers the GUI tracks. */
#define AA_SCREEN_BUFFERS 2

/**
 * @brief Claim the display and show a blank picture on it.
 *
 * @retval 0 Success.
 * @retval -ENODEV The display is not ready.
 */
int aa_screen_init(void);

/**
 * @brief The RGB565 framebuffer, for a decoder that writes pixels itself.
 *
 * Only meaningful where the display controller is not converting the decoder's
 * samples, which is the only case a decoder writes RGB into.
 */
uint16_t *aa_screen_framebuffer(void);

/**
 * @brief Compose one picture with the GUI and show the result.
 *
 * @param pic  Planar YUV420 picture, or NULL to leave the video area black.
 * @param w,h  Picture size in pixels.
 */
void aa_screen_show(const uint8_t *pic, uint16_t w, uint16_t h);

/** @brief Show what a decoder has already written to the framebuffer. */
void aa_screen_push(void);

/**
 * @brief Turn the panel off, or back on.
 *
 * @param on true to blank it, false to show what it holds again.
 */
void aa_screen_blank(bool on);

/** @brief When a picture was last shown, in uptime milliseconds. */
int64_t aa_screen_last_picture(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SCREEN_H_ */
