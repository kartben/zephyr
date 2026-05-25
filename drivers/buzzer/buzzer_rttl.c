/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/buzzer.h>
#include <zephyr/kernel.h>

struct buzzer_rttl_defaults {
	uint8_t duration;
	uint8_t octave;
	uint16_t bpm;
};

struct buzzer_rttl_call {
	uint32_t freq_hz;
	uint32_t duration_ms;
};

static const uint16_t buzzer_rttl_note_freq_hz[][12] = {
	{ 262U, 277U, 294U, 311U, 330U, 349U, 370U, 392U, 415U, 440U, 466U, 494U },
	{ 523U, 554U, 587U, 622U, 659U, 698U, 740U, 784U, 831U, 880U, 932U, 988U },
	{ 1047U, 1109U, 1175U, 1245U, 1319U, 1397U, 1480U, 1568U, 1661U, 1760U,
	  1865U, 1976U },
	{ 2093U, 2217U, 2349U, 2489U, 2637U, 2794U, 2960U, 3136U, 3322U, 3520U,
	  3729U, 3951U },
};

static void buzzer_rttl_skip_spaces(const char **str)
{
	while (isspace((unsigned char)**str) != 0) {
		(*str)++;
	}
}

static int buzzer_rttl_parse_number(const char **str, uint32_t *value)
{
	uint64_t result = 0ULL;
	bool found = false;

	while (isdigit((unsigned char)**str) != 0) {
		found = true;
		result = (result * 10U) + (uint32_t)(**str - '0');
		if (result > UINT32_MAX) {
			return -EINVAL;
		}
		(*str)++;
	}

	if (!found) {
		return -EINVAL;
	}

	*value = (uint32_t)result;
	return 0;
}

static int buzzer_rttl_parse_defaults(const char **str,
				      struct buzzer_rttl_defaults *defaults)
{
	const char *cursor = *str;
	int ret;

	defaults->duration = 4U;
	defaults->octave = 6U;
	defaults->bpm = 63U;

	buzzer_rttl_skip_spaces(&cursor);

	while ((*cursor != '\0') && (*cursor != ':')) {
		uint32_t value;
		char key = tolower((unsigned char)*cursor);

		cursor++;
		buzzer_rttl_skip_spaces(&cursor);
		if (*cursor != '=') {
			return -EINVAL;
		}

		cursor++;
		buzzer_rttl_skip_spaces(&cursor);

		ret = buzzer_rttl_parse_number(&cursor, &value);
		if (ret < 0) {
			return ret;
		}

		switch (key) {
		case 'd':
			if ((value == 0U) || (value > UINT8_MAX)) {
				return -EINVAL;
			}
			defaults->duration = (uint8_t)value;
			break;
		case 'o':
			if ((value < 4U) || (value > 7U)) {
				return -EINVAL;
			}
			defaults->octave = (uint8_t)value;
			break;
		case 'b':
			if ((value == 0U) || (value > UINT16_MAX)) {
				return -EINVAL;
			}
			defaults->bpm = (uint16_t)value;
			break;
		default:
			return -EINVAL;
		}

		buzzer_rttl_skip_spaces(&cursor);
		if (*cursor == ',') {
			cursor++;
			buzzer_rttl_skip_spaces(&cursor);
		} else if (*cursor != ':') {
			return -EINVAL;
		}
	}

	if (*cursor != ':') {
		return -EINVAL;
	}

	*str = cursor + 1;
	return 0;
}

static uint32_t buzzer_rttl_note_frequency(char note, bool sharp, uint8_t octave)
{
	uint8_t index;

	switch (note) {
	case 'c':
		index = 0U;
		break;
	case 'd':
		index = 2U;
		break;
	case 'e':
		index = 4U;
		break;
	case 'f':
		index = 5U;
		break;
	case 'g':
		index = 7U;
		break;
	case 'a':
		index = 9U;
		break;
	case 'b':
		index = 11U;
		break;
	default:
		return 0U;
	}

	if (sharp) {
		if ((note == 'e') || (note == 'b')) {
			return 0U;
		}
		index++;
	}

	return buzzer_rttl_note_freq_hz[octave - 4U][index];
}

static uint32_t buzzer_rttl_note_duration_ms(uint16_t bpm, uint32_t duration, bool dotted)
{
	uint64_t duration_ms;

	duration_ms = 240000ULL / (uint64_t)bpm;
	duration_ms /= duration;
	if (duration_ms == 0U) {
		duration_ms = 1U;
	}

	if (dotted) {
		duration_ms += duration_ms / 2U;
	}

	return (uint32_t)duration_ms;
}

static int buzzer_rttl_parse_call(const char **str,
				  const struct buzzer_rttl_defaults *defaults,
				  struct buzzer_rttl_call *call)
{
	const char *cursor = *str;
	uint32_t duration = defaults->duration;
	uint32_t octave = defaults->octave;
	uint32_t value;
	char note;
	bool dotted = false;
	bool sharp = false;
	int ret;

	buzzer_rttl_skip_spaces(&cursor);
	if (*cursor == '\0') {
		return 1;
	}

	if (isdigit((unsigned char)*cursor) != 0) {
		ret = buzzer_rttl_parse_number(&cursor, &value);
		if (ret < 0) {
			return ret;
		}
		if (value == 0U) {
			return -EINVAL;
		}
		duration = value;
	}

	note = tolower((unsigned char)*cursor);
	if (note == '\0') {
		return -EINVAL;
	}

	cursor++;
	if ((note != 'p') && ((note < 'a') || (note > 'g'))) {
		return -EINVAL;
	}

	if (*cursor == '#') {
		if (note == 'p') {
			return -EINVAL;
		}
		sharp = true;
		cursor++;
	}

	if (*cursor == '.') {
		dotted = true;
		cursor++;
	}

	if (isdigit((unsigned char)*cursor) != 0) {
		ret = buzzer_rttl_parse_number(&cursor, &value);
		if (ret < 0) {
			return ret;
		}
		if ((value < 4U) || (value > 7U)) {
			return -EINVAL;
		}
		octave = value;
	}

	if (*cursor == '.') {
		if (dotted) {
			return -EINVAL;
		}
		dotted = true;
		cursor++;
	}

	call->duration_ms = buzzer_rttl_note_duration_ms(defaults->bpm, duration, dotted);
	if (note == 'p') {
		call->freq_hz = BUZZER_FREQ_REST;
	} else {
		call->freq_hz = buzzer_rttl_note_frequency(note, sharp, (uint8_t)octave);
		if (call->freq_hz == 0U) {
			return -EINVAL;
		}
	}

	buzzer_rttl_skip_spaces(&cursor);
	if (*cursor == ',') {
		cursor++;
		buzzer_rttl_skip_spaces(&cursor);
		if (*cursor == '\0') {
			return -EINVAL;
		}
	} else if (*cursor != '\0') {
		return -EINVAL;
	}

	*str = cursor;
	return 0;
}

int buzzer_play_rttl(const struct device *dev, const char *rttl)
{
	struct buzzer_rttl_defaults defaults;
	struct buzzer_rttl_call call;
	const char *cursor = rttl;
	int ret;

	if ((dev == NULL) || (rttl == NULL)) {
		return -EINVAL;
	}

	while ((*cursor != '\0') && (*cursor != ':')) {
		cursor++;
	}

	if ((cursor == rttl) || (*cursor != ':')) {
		return -EINVAL;
	}

	cursor++;
	ret = buzzer_rttl_parse_defaults(&cursor, &defaults);
	if ((ret < 0) || (*cursor == '\0')) {
		return -EINVAL;
	}

	while (true) {
		ret = buzzer_rttl_parse_call(&cursor, &defaults, &call);
		if (ret == 1) {
			break;
		}
		if (ret < 0) {
			(void)buzzer_stop(dev);
			return ret;
		}

		if (call.freq_hz == BUZZER_FREQ_REST) {
			ret = buzzer_stop(dev);
		} else {
			ret = buzzer_tone(dev, call.freq_hz, call.duration_ms);
		}
		if (ret < 0) {
			(void)buzzer_stop(dev);
			return ret;
		}

		k_sleep(K_MSEC(call.duration_ms));
	}

	return buzzer_stop(dev);
}
