/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <lvgl.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sim, LOG_LEVEL_INF);

#include "agent_pad.h"

#define KEY_W	 82
#define KEY_H	 52
#define KEY_GAP	 8
#define PAD_X	 210
#define MARGIN	 18

lv_obj_t *sim_build(lv_obj_t *root);

static lv_obj_t *keys[12];
static lv_obj_t *key_labels[12];
static lv_obj_t *sent_label;
static lv_obj_t *chime_label;

static char sent_text[32] = "sent: -";
static char chime_text[32] = "chime: -";

int hid_kb_init(void)
{
	return 0;
}

void hid_kb_run(const struct key_action *action)
{
	snprintf(sent_text, sizeof(sent_text), "sent: %s", action->name);
}

int leds_init(void)
{
	return 0;
}

void leds_flash(int key)
{
	ARG_UNUSED(key);
}

int chime_init(void)
{
	return 0;
}

void chime_play(enum chime chime)
{
	static const char *const names[] = {
		[CHIME_DONE] = "done",
		[CHIME_INPUT] = "input",
		[CHIME_ERROR] = "error",
		[CHIME_LAYER] = "layer",
	};

	snprintf(chime_text, sizeof(chime_text), "chime: %s", names[chime]);
}

static lv_color_t status_color(enum agent_status status)
{
	switch (status) {
	case AGENT_IDLE:
		return lv_color_make(0x60, 0x60, 0x60);
	case AGENT_THINKING:
		return lv_color_make(0x30, 0x78, 0xff);
	case AGENT_DONE:
		return lv_color_make(0x28, 0xc8, 0x5a);
	case AGENT_INPUT:
		return lv_color_make(0xff, 0xaa, 0x28);
	case AGENT_ERROR:
		return lv_color_make(0xe6, 0x3c, 0x3c);
	default:
		return lv_color_make(0x23, 0x26, 0x2c);
	}
}

static void key_event_cb(lv_event_t *e)
{
	int idx = (int)(intptr_t)lv_event_get_user_data(e);
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_PRESSED) {
		input_report_key(NULL, pad_key_codes[idx], 1, true, K_NO_WAIT);
	} else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		input_report_key(NULL, pad_key_codes[idx], 0, true, K_NO_WAIT);
	}
}

static void knob_event_cb(lv_event_t *e)
{
	lv_event_code_t code = lv_event_get_code(e);

	if (code == LV_EVENT_PRESSED) {
		input_report_key(NULL, INPUT_KEY_ENTER, 1, true, K_NO_WAIT);
	} else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
		input_report_key(NULL, INPUT_KEY_ENTER, 0, true, K_NO_WAIT);
	}
}

static void turn_event_cb(lv_event_t *e)
{
	input_report_rel(NULL, INPUT_REL_WHEEL, (int)(intptr_t)lv_event_get_user_data(e), true,
			 K_NO_WAIT);
}

static void sim_timer_cb(lv_timer_t *timer)
{
	const struct layer *layer = &layers[state_layer()];
	struct agent agent;

	for (int i = 0; i < AGENT_SLOTS; i++) {
		state_agent_get(i, &agent);
		lv_obj_set_style_bg_color(keys[i], status_color(agent.status), 0);
	}

	for (int i = 0; i < 6; i++) {
		lv_obj_set_style_bg_color(keys[AGENT_SLOTS + i],
					  lv_color_make(layer->color[0] / 3, layer->color[1] / 3,
							layer->color[2] / 3),
					  0);
		lv_label_set_text(key_labels[AGENT_SLOTS + i], layer->keys[i].name);
	}

	lv_label_set_text(sent_label, sent_text);
	lv_label_set_text(chime_label, chime_text);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, lv_event_cb_t cb, intptr_t arg,
			     lv_obj_t **label_out)
{
	lv_obj_t *btn = lv_button_create(parent);
	lv_obj_t *label = lv_label_create(btn);

	lv_label_set_text(label, text);
	lv_obj_center(label);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_ALL, (void *)arg);
	if (label_out != NULL) {
		*label_out = label;
	}

	return btn;
}

lv_obj_t *sim_build(lv_obj_t *root)
{
	lv_obj_t *oled;
	lv_obj_t *btn;

	lv_obj_set_style_bg_color(root, lv_color_make(0x14, 0x16, 0x1a), 0);
	lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);

	oled = lv_obj_create(root);
	lv_obj_remove_style_all(oled);
	lv_obj_set_pos(oled, MARGIN, MARGIN);
	lv_obj_set_size(oled, 128, 64);
	lv_obj_set_style_outline_width(oled, 2, 0);
	lv_obj_set_style_outline_pad(oled, 3, 0);
	lv_obj_set_style_outline_color(oled, lv_color_make(0x40, 0x46, 0x50), 0);

	sent_label = lv_label_create(root);
	lv_obj_set_pos(sent_label, MARGIN, 110);
	lv_obj_set_style_text_color(sent_label, lv_color_white(), 0);

	chime_label = lv_label_create(root);
	lv_obj_set_pos(chime_label, MARGIN, 130);
	lv_obj_set_style_text_color(chime_label, lv_color_make(0x8b, 0x93, 0xa1), 0);

	btn = make_button(root, LV_SYMBOL_LEFT, turn_event_cb, -1, NULL);
	lv_obj_set_pos(btn, MARGIN, 170);
	lv_obj_set_size(btn, 44, 40);

	btn = make_button(root, "KNOB", knob_event_cb, 0, NULL);
	lv_obj_set_pos(btn, MARGIN + 52, 170);
	lv_obj_set_size(btn, 60, 40);

	btn = make_button(root, LV_SYMBOL_RIGHT, turn_event_cb, 1, NULL);
	lv_obj_set_pos(btn, MARGIN + 120, 170);
	lv_obj_set_size(btn, 44, 40);

	for (int i = 0; i < 12; i++) {
		char text[4];

		snprintf(text, sizeof(text), "A%d", i + 1);
		btn = make_button(root, i < AGENT_SLOTS ? text : "", key_event_cb, i,
				  &key_labels[i]);
		lv_obj_set_pos(btn, PAD_X + (i % 3) * (KEY_W + KEY_GAP),
			       MARGIN + (i / 3) * (KEY_H + KEY_GAP));
		lv_obj_set_size(btn, KEY_W, KEY_H);
		lv_obj_set_style_radius(btn, 6, 0);
		keys[i] = btn;
	}

	lv_timer_create(sim_timer_cb, 150, NULL);

	return oled;
}
