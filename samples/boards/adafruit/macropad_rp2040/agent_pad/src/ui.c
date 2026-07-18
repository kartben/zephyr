/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#include "agent_pad.h"

#define ROW_H 16

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static char status_char(enum agent_status status, int tick)
{
	static const char spinner[] = "|/-\\";

	switch (status) {
	case AGENT_IDLE:
		return 'o';
	case AGENT_THINKING:
		return spinner[tick % 4];
	case AGENT_DONE:
		return '+';
	case AGENT_INPUT:
		return '?';
	case AGENT_ERROR:
		return '!';
	default:
		return '.';
	}
}

static void redraw(int tick)
{
	char line[16];
	char note[NOTE_LEN];
	char status[AGENT_SLOTS];
	struct agent agent;

	for (int i = 0; i < AGENT_SLOTS; i++) {
		state_agent_get(i, &agent);
		status[i] = status_char(agent.status, tick);
	}
	state_note_get(note, sizeof(note));

	cfb_framebuffer_clear(display, false);

	snprintf(line, sizeof(line), " %-8s L%d", layers[state_layer()].name,
		 state_layer() + 1);
	cfb_print(display, line, 0, 0);
	cfb_invert_area(display, 0, 0, 128, ROW_H);

	cfb_print(display, "1 2 3 4 5 6", 4, ROW_H);
	snprintf(line, sizeof(line), "%c %c %c %c %c %c", status[0], status[1], status[2],
		 status[3], status[4], status[5]);
	cfb_print(display, line, 4, 2 * ROW_H);
	cfb_print(display, note, 0, 3 * ROW_H);

	cfb_framebuffer_finalize(display);
}

static void ui_thread(void *p1, void *p2, void *p3)
{
	uint32_t seen = 0;
	int tick = 0;
	bool thinking;

	while (true) {
		struct agent agent;

		thinking = false;
		for (int i = 0; i < AGENT_SLOTS; i++) {
			state_agent_get(i, &agent);
			thinking = thinking || (agent.status == AGENT_THINKING);
		}

		if (state_seq() != seen || thinking) {
			seen = state_seq();
			redraw(tick);
		}

		tick++;
		k_msleep(250);
	}
}

K_THREAD_DEFINE(ui_tid, 2048, ui_thread, NULL, NULL, NULL, 8, 0, K_TICKS_FOREVER);

int ui_init(void)
{
	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return -EIO;
	}

	if (cfb_framebuffer_init(display) != 0) {
		LOG_ERR("Framebuffer init failed");
		return -EIO;
	}

	state_note_set("agents ready");
	redraw(0);

	k_thread_start(ui_tid);

	return 0;
}
