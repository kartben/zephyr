/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#include "agent_pad.h"

#define LAYER_HOLD_MS 600
#define CONFIRM_MS    3000

struct pad_event {
	uint16_t code;
	int32_t value;
};

K_MSGQ_DEFINE(pad_msgq, sizeof(struct pad_event), 8, 4);

static void input_cb(struct input_event *evt, void *user_data)
{
	struct pad_event pad_evt = {.code = evt->code, .value = evt->value};

	if (evt->type != INPUT_EV_KEY && evt->type != INPUT_EV_REL) {
		return;
	}

	if (k_msgq_put(&pad_msgq, &pad_evt, K_NO_WAIT) != 0) {
		LOG_WRN("Event queue full, dropped event");
	}
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

static void run_action(const struct key_action *action)
{
	state_note_set("%s", action->name);
	hid_kb_run(action);
}

static int armed_key = -1;

static void arm_clear(void)
{
	if (armed_key < 0) {
		return;
	}

	armed_key = -1;
	leds_arm(-1);
	state_disarm();
}

static void arm_expired(struct k_work *work)
{
	ARG_UNUSED(work);
	arm_clear();
}

static K_WORK_DELAYABLE_DEFINE(arm_work, arm_expired);

/*
 * Waits out a timeout that is already running, so that it cannot clear an arm
 * made moments later. Only safe to call from outside the work queue.
 */
static void disarm(void)
{
	struct k_work_sync sync;

	k_work_cancel_delayable_sync(&arm_work, &sync);
	arm_clear();
}

/*
 * The keys that type into the agent are armed by a first press and only run
 * on a second, so an unfamiliar layer can be read off the display without
 * firing anything. Pressing a different key arms that one instead.
 */
static void handle_action_key(int key)
{
	const struct key_action *action = &layers[state_layer()].keys[key - AGENT_SLOTS];
	bool confirmed = armed_key == key;

	leds_flash(key);
	disarm();

	if (confirmed) {
		run_action(action);
		return;
	}

	armed_key = key;
	leds_arm(key);
	state_arm(action->name);
	k_work_reschedule(&arm_work, K_MSEC(CONFIRM_MS));
}

static void handle_agent_key(int slot)
{
	struct agent agent;

	leds_flash(slot);
	link_notify_keypress(slot);

	state_agent_get(slot, &agent);
	if (agent.status == AGENT_DONE || agent.status == AGENT_INPUT ||
	    agent.status == AGENT_ERROR) {
		state_agent_set(slot, AGENT_IDLE, NULL);
		state_note_set("A%d ack", slot + 1);
	}
}

static void handle_encoder_button(int32_t value)
{
	static int64_t pressed_at;

	if (value != 0) {
		pressed_at = k_uptime_get();
		return;
	}

	if (k_uptime_get() - pressed_at >= LAYER_HOLD_MS) {
		/* The armed key means something else on the next layer. */
		disarm();
		state_layer_next();
		state_note_set("%s", layers[state_layer()].name);
		chime_play(CHIME_LAYER);
	} else {
		run_action(&layers[state_layer()].enc_press);
	}
}

static int key_index(uint16_t code)
{
	for (int i = 0; i < (int)ARRAY_SIZE(pad_key_codes); i++) {
		if (pad_key_codes[i] == code) {
			return i;
		}
	}

	return -1;
}

static void handle_event(const struct pad_event *evt)
{
	const struct layer *layer = &layers[state_layer()];
	int key = key_index(evt->code);

	if (key >= 0) {
		if (evt->value == 0) {
			return;
		}
		if (key < AGENT_SLOTS) {
			handle_agent_key(key);
		} else {
			handle_action_key(key);
		}
		return;
	}

	switch (evt->code) {
	case INPUT_KEY_ENTER:
		handle_encoder_button(evt->value);
		break;
	case INPUT_REL_WHEEL:
		for (int i = 0; i < abs(evt->value); i++) {
			run_action(evt->value > 0 ? &layer->enc_cw : &layer->enc_ccw);
		}
		break;
	default:
		break;
	}
}

/*
 * Everything here is optional: a pad with no USB host still shows agent
 * status, and one with a dead speaker still types. Failures are reported on
 * the display rather than taken as fatal.
 */
static const struct {
	const char *name;
	int (*init)(void);
} subsystems[] = {
	{"usb", hid_kb_init},
	{"link", link_init},
	{"leds", leds_init},
	{"audio", chime_init},
};

int main(void)
{
	struct pad_event evt;

	/*
	 * The display is the only output that does not need a host, so it
	 * comes up first and is the one hard dependency.
	 */
	if (ui_init() != 0) {
		return -EIO;
	}

	for (size_t i = 0; i < ARRAY_SIZE(subsystems); i++) {
		int ret = subsystems[i].init();

		if (ret != 0) {
			LOG_WRN("%s unavailable (%d)", subsystems[i].name, ret);
			state_note_set("no %s %d", subsystems[i].name, ret);
		}
	}

	LOG_INF("Agent pad ready");

	while (true) {
		k_msgq_get(&pad_msgq, &evt, K_FOREVER);
		handle_event(&evt);
	}

	return 0;
}
