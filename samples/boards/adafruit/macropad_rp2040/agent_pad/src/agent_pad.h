/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AGENT_PAD_H_
#define AGENT_PAD_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AGENT_SLOTS	6
#define AGENT_LABEL_LEN	12
#define NOTE_LEN	13

enum agent_status {
	AGENT_EMPTY,
	AGENT_IDLE,
	AGENT_THINKING,
	AGENT_DONE,
	AGENT_INPUT,
	AGENT_ERROR,
};

struct agent {
	enum agent_status status;
	char label[AGENT_LABEL_LEN];
};

enum agent_status state_agent_set(int slot, enum agent_status status, const char *label);
void state_agent_get(int slot, struct agent *out);
bool state_all_empty(void);
int state_layer(void);
void state_layer_next(void);
void state_note_set(const char *fmt, ...);
void state_note_get(char *out, size_t len);
void state_arm(const char *name);
void state_disarm(void);
bool state_armed(void);
uint32_t state_seq(void);

struct key_tap {
	uint8_t mod;
	uint8_t code;
};

struct key_action {
	const char *name;
	const struct key_tap *taps;
	size_t num_taps;
	const char *text;
};

struct layer {
	const char *name;
	uint8_t color[3];
	struct key_action keys[6];
	struct key_action enc_cw;
	struct key_action enc_ccw;
	struct key_action enc_press;
};

extern const struct layer layers[];
extern const size_t num_layers;
extern const uint16_t pad_key_codes[12];

int hid_kb_init(void);
void hid_kb_run(const struct key_action *action);

int link_init(void);
void link_notify_keypress(int slot);

int leds_init(void);
void leds_flash(int key);
void leds_arm(int key);

int ui_init(void);

enum chime {
	CHIME_DONE,
	CHIME_INPUT,
	CHIME_ERROR,
	CHIME_LAYER,
};

int chime_init(void);
void chime_play(enum chime chime);

#endif /* AGENT_PAD_H_ */
