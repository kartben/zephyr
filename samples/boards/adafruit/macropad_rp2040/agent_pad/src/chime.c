/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/buzzer.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(chime, LOG_LEVEL_INF);

#include "agent_pad.h"

#define VOLUME_PERCENT 25
#define MAX_NOTES 3

struct note {
	uint16_t freq_hz;
	uint16_t duration_ms;
};

static const struct note chimes[][MAX_NOTES] = {
	[CHIME_DONE] = {{1568, 70}},
	[CHIME_INPUT] = {{880, 70}, {1175, 110}},
	[CHIME_ERROR] = {{196, 120}, {BUZZER_FREQ_REST, 30}, {196, 120}},
	[CHIME_LAYER] = {{1319, 40}},
};

static const struct device *buzzer = DEVICE_DT_GET(DT_NODELABEL(buzzer));
static const struct device *speaker_reg = DEVICE_DT_GET(DT_NODELABEL(speaker_reg));

static const struct note *current;
static int note_idx;
static bool ready;

static void play_note(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(note_work, play_note);

static void play_note(struct k_work *work)
{
	const struct note *note;

	if (current == NULL || note_idx >= MAX_NOTES || current[note_idx].duration_ms == 0) {
		current = NULL;
		return;
	}

	note = &current[note_idx++];
	buzzer_tone(buzzer, note->freq_hz, note->duration_ms);
	k_work_schedule(&note_work, K_MSEC(note->duration_ms));
}

void chime_play(enum chime chime)
{
	if (!ready || current != NULL) {
		return;
	}

	current = chimes[chime];
	note_idx = 0;
	k_work_schedule(&note_work, K_NO_WAIT);
}

int chime_init(void)
{
	int ret;

	if (!device_is_ready(buzzer) || !device_is_ready(speaker_reg)) {
		LOG_ERR("Speaker devices not ready");
		return -EIO;
	}

	buzzer_set_volume(buzzer, VOLUME_PERCENT);

	ret = regulator_enable(speaker_reg);
	if (ret != 0) {
		return ret;
	}

	ready = true;

	return 0;
}
