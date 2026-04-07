/*
 * Copyright (c) 2024
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <SDL.h>
#include "input_sdl_keyboard_bottom.h"

/* Map SDL scancodes to Zephyr input key codes.
 * Zephyr uses Linux input event codes (from dt-bindings/input/input-event-codes.h).
 */
static uint16_t sdl_scancode_to_input_key(SDL_Scancode sc)
{
	switch (sc) {
	case SDL_SCANCODE_A:      return 30;  /* INPUT_KEY_A */
	case SDL_SCANCODE_B:      return 48;  /* INPUT_KEY_B */
	case SDL_SCANCODE_C:      return 46;  /* INPUT_KEY_C */
	case SDL_SCANCODE_D:      return 32;  /* INPUT_KEY_D */
	case SDL_SCANCODE_E:      return 18;  /* INPUT_KEY_E */
	case SDL_SCANCODE_F:      return 33;  /* INPUT_KEY_F */
	case SDL_SCANCODE_G:      return 34;  /* INPUT_KEY_G */
	case SDL_SCANCODE_H:      return 35;  /* INPUT_KEY_H */
	case SDL_SCANCODE_I:      return 23;  /* INPUT_KEY_I */
	case SDL_SCANCODE_J:      return 36;  /* INPUT_KEY_J */
	case SDL_SCANCODE_K:      return 37;  /* INPUT_KEY_K */
	case SDL_SCANCODE_L:      return 38;  /* INPUT_KEY_L */
	case SDL_SCANCODE_M:      return 50;  /* INPUT_KEY_M */
	case SDL_SCANCODE_N:      return 49;  /* INPUT_KEY_N */
	case SDL_SCANCODE_O:      return 24;  /* INPUT_KEY_O */
	case SDL_SCANCODE_P:      return 25;  /* INPUT_KEY_P */
	case SDL_SCANCODE_Q:      return 16;  /* INPUT_KEY_Q */
	case SDL_SCANCODE_R:      return 19;  /* INPUT_KEY_R */
	case SDL_SCANCODE_S:      return 31;  /* INPUT_KEY_S */
	case SDL_SCANCODE_T:      return 20;  /* INPUT_KEY_T */
	case SDL_SCANCODE_U:      return 22;  /* INPUT_KEY_U */
	case SDL_SCANCODE_V:      return 47;  /* INPUT_KEY_V */
	case SDL_SCANCODE_W:      return 17;  /* INPUT_KEY_W */
	case SDL_SCANCODE_X:      return 45;  /* INPUT_KEY_X */
	case SDL_SCANCODE_Y:      return 21;  /* INPUT_KEY_Y */
	case SDL_SCANCODE_Z:      return 44;  /* INPUT_KEY_Z */
	case SDL_SCANCODE_1:      return 2;   /* INPUT_KEY_1 */
	case SDL_SCANCODE_2:      return 3;   /* INPUT_KEY_2 */
	case SDL_SCANCODE_3:      return 4;   /* INPUT_KEY_3 */
	case SDL_SCANCODE_4:      return 5;   /* INPUT_KEY_4 */
	case SDL_SCANCODE_5:      return 6;   /* INPUT_KEY_5 */
	case SDL_SCANCODE_6:      return 7;   /* INPUT_KEY_6 */
	case SDL_SCANCODE_7:      return 8;   /* INPUT_KEY_7 */
	case SDL_SCANCODE_8:      return 9;   /* INPUT_KEY_8 */
	case SDL_SCANCODE_9:      return 10;  /* INPUT_KEY_9 */
	case SDL_SCANCODE_0:      return 11;  /* INPUT_KEY_0 */
	case SDL_SCANCODE_RETURN: return 28;  /* INPUT_KEY_ENTER */
	case SDL_SCANCODE_ESCAPE: return 1;   /* INPUT_KEY_ESC */
	case SDL_SCANCODE_SPACE:  return 57;  /* INPUT_KEY_SPACE */
	case SDL_SCANCODE_TAB:    return 15;  /* INPUT_KEY_TAB */
	case SDL_SCANCODE_UP:     return 103; /* INPUT_KEY_UP */
	case SDL_SCANCODE_DOWN:   return 108; /* INPUT_KEY_DOWN */
	case SDL_SCANCODE_LEFT:   return 105; /* INPUT_KEY_LEFT */
	case SDL_SCANCODE_RIGHT:  return 106; /* INPUT_KEY_RIGHT */
	case SDL_SCANCODE_LSHIFT: return 42;  /* INPUT_KEY_LEFTSHIFT */
	case SDL_SCANCODE_RSHIFT: return 54;  /* INPUT_KEY_RIGHTSHIFT */
	case SDL_SCANCODE_LCTRL:  return 29;  /* INPUT_KEY_LEFTCTRL */
	case SDL_SCANCODE_RCTRL:  return 97;  /* INPUT_KEY_RIGHTCTRL */
	default:                  return 0;
	}
}

static int sdl_keyboard_filter(void *arg, SDL_Event *event)
{
	struct sdl_keyboard_data *data = arg;
	uint16_t key_code;

	switch (event->type) {
	case SDL_KEYDOWN:
		if (event->key.repeat) {
			return 1;
		}
		key_code = sdl_scancode_to_input_key(event->key.keysym.scancode);
		if (key_code == 0) {
			return 1;
		}
		data->key_code = key_code;
		data->pressed = true;
		data->callback(data);
		break;
	case SDL_KEYUP:
		key_code = sdl_scancode_to_input_key(event->key.keysym.scancode);
		if (key_code == 0) {
			return 1;
		}
		data->key_code = key_code;
		data->pressed = false;
		data->callback(data);
		break;
	default:
		return 1;
	}

	return 1;
}

void sdl_keyboard_init_bottom(struct sdl_keyboard_data *data)
{
	SDL_AddEventWatch(sdl_keyboard_filter, data);
}
