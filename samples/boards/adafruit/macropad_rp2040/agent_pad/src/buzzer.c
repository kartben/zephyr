/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(buzzer, LOG_LEVEL_INF);

#include "agent_pad.h"

#define SPEAKER_PWM_CHANNEL 0
#define MAX_NOTES 3

struct note {
	uint16_t freq_hz;
	uint16_t duration_ms;
};

static const struct note chimes[][MAX_NOTES] = {
	[CHIME_DONE] = {{1568, 70}},
	[CHIME_INPUT] = {{880, 70}, {1175, 110}},
	[CHIME_ERROR] = {{196, 120}, {0, 30}, {196, 120}},
	[CHIME_LAYER] = {{1319, 40}},
};

static const struct device *pwm_dev = DEVICE_DT_GET(DT_NODELABEL(pwm));
static const struct device *speaker_reg = DEVICE_DT_GET(DT_NODELABEL(speaker_reg));

static const struct note *current;
static int note_idx;

static void play_note(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(note_work, play_note);

static void tone(uint16_t freq_hz)
{
	if (freq_hz == 0) {
		pwm_set(pwm_dev, SPEAKER_PWM_CHANNEL, PWM_HZ(100), 0, 0);
		return;
	}

	pwm_set(pwm_dev, SPEAKER_PWM_CHANNEL, PWM_HZ(freq_hz), PWM_HZ(freq_hz) / 8, 0);
}

static void play_note(struct k_work *work)
{
	if (current == NULL || note_idx >= MAX_NOTES || current[note_idx].duration_ms == 0) {
		tone(0);
		current = NULL;
		return;
	}

	tone(current[note_idx].freq_hz);
	k_work_schedule(&note_work, K_MSEC(current[note_idx].duration_ms));
	note_idx++;
}

void buzzer_play(enum chime chime)
{
	if (current != NULL) {
		return;
	}

	current = chimes[chime];
	note_idx = 0;
	k_work_schedule(&note_work, K_NO_WAIT);
}

int buzzer_init(void)
{
	if (!device_is_ready(pwm_dev) || !device_is_ready(speaker_reg)) {
		LOG_ERR("Speaker devices not ready");
		return -EIO;
	}

	return regulator_enable(speaker_reg);
}
