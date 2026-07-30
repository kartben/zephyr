/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/usb/class/hid.h>

#include "agent_pad.h"

const uint16_t pad_key_codes[12] = {
	INPUT_KEY_F1, INPUT_KEY_F2,  INPUT_KEY_F3,  INPUT_KEY_F4,
	INPUT_KEY_F5, INPUT_KEY_F6,  INPUT_KEY_F7,  INPUT_KEY_F8,
	INPUT_KEY_F9, INPUT_KEY_F10, INPUT_KEY_F11, INPUT_KEY_F12,
};

#define NUM(_a) (sizeof(_a) / sizeof((_a)[0]))

#define TAPS(_name, ...)						\
	static const struct key_tap _name[] = {__VA_ARGS__}

#define KEY_TAPS(_label, _taps)						\
	{.name = _label, .taps = _taps, .num_taps = NUM(_taps)}

#define KEY_TEXT(_label, _text)						\
	{.name = _label, .text = _text}

TAPS(tap_esc, {0, HID_KEY_ESC});
TAPS(tap_mode, {HID_KBD_MODIFIER_LEFT_SHIFT, HID_KEY_TAB});
TAPS(tap_transcript, {HID_KBD_MODIFIER_LEFT_CTRL, HID_KEY_R});
TAPS(tap_up, {0, HID_KEY_UP});
TAPS(tap_down, {0, HID_KEY_DOWN});
TAPS(tap_enter, {0, HID_KEY_ENTER});

const struct layer layers[] = {
	{
		.name = "CLAUDE",
		.color = {0xff, 0x50, 0x00},
		.keys = {
			KEY_TAPS("stop", tap_esc),
			KEY_TAPS("mode", tap_mode),
			KEY_TAPS("verbose", tap_transcript),
			KEY_TAPS("history", tap_up),
			KEY_TAPS("accept", tap_enter),
			KEY_TEXT("claude", "claude\n"),
		},
		.enc_cw = KEY_TAPS("down", tap_down),
		.enc_ccw = KEY_TAPS("up", tap_up),
		.enc_press = KEY_TAPS("enter", tap_enter),
	},
	{
		.name = "SLASH",
		.color = {0x00, 0x50, 0xff},
		.keys = {
			KEY_TEXT("/compact", "/compact\n"),
			KEY_TEXT("/clear", "/clear\n"),
			KEY_TEXT("/model", "/model\n"),
			KEY_TEXT("/resume", "/resume\n"),
			KEY_TEXT("/review", "/review\n"),
			KEY_TEXT("/help", "/help\n"),
		},
		.enc_cw = KEY_TAPS("down", tap_down),
		.enc_ccw = KEY_TAPS("up", tap_up),
		.enc_press = KEY_TAPS("enter", tap_enter),
	},
};

const size_t num_layers = NUM(layers);
