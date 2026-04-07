/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * "Bottom" of the SDL keyboard input driver.
 * When built with the native_simulator this will be built in the runner context,
 * that is, with the host C library, and with the host include paths.
 */

#ifndef DRIVERS_INPUT_INPUT_SDL_KEYBOARD_BOTTOM_H
#define DRIVERS_INPUT_INPUT_SDL_KEYBOARD_BOTTOM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct sdl_keyboard_data {
	const void *dev;
	void (*callback)(struct sdl_keyboard_data *data);
	uint16_t key_code;
	bool pressed;
};

void sdl_keyboard_init_bottom(struct sdl_keyboard_data *data);

#ifdef __cplusplus
}
#endif

#endif /* DRIVERS_INPUT_INPUT_SDL_KEYBOARD_BOTTOM_H */
