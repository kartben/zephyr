/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/buzzer.h>
#include <zephyr/ztest.h>

enum fake_call_type {
	FAKE_CALL_TONE,
	FAKE_CALL_STOP,
};

struct fake_call {
	enum fake_call_type type;
	uint32_t freq_hz;
	uint32_t duration_ms;
};

static struct fake_call fake_calls[8];
static size_t fake_call_count;
static int fake_tone_ret;
static int fake_stop_ret;

static int fake_buzzer_tone(const struct device *dev, uint32_t freq_hz, uint32_t duration_ms)
{
	ARG_UNUSED(dev);

	fake_calls[fake_call_count].type = FAKE_CALL_TONE;
	fake_calls[fake_call_count].freq_hz = freq_hz;
	fake_calls[fake_call_count].duration_ms = duration_ms;
	fake_call_count++;

	return fake_tone_ret;
}

static int fake_buzzer_set_volume(const struct device *dev, uint8_t percent)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(percent);

	return 0;
}

static int fake_buzzer_beep(const struct device *dev, uint32_t duration_ms)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(duration_ms);

	return 0;
}

static int fake_buzzer_stop(const struct device *dev)
{
	ARG_UNUSED(dev);

	fake_calls[fake_call_count].type = FAKE_CALL_STOP;
	fake_call_count++;

	return fake_stop_ret;
}

static DEVICE_API(buzzer, fake_buzzer_api) = {
	.tone = fake_buzzer_tone,
	.set_volume = fake_buzzer_set_volume,
	.beep = fake_buzzer_beep,
	.stop = fake_buzzer_stop,
};

static struct device_state fake_buzzer_state = {
	.init_res = 0,
	.initialized = 1,
};

static const struct device fake_buzzer = {
	.name = "fake-buzzer",
	.api = &fake_buzzer_api,
	.state = &fake_buzzer_state,
};

static void reset_fake_buzzer(void)
{
	(void)memset(fake_calls, 0, sizeof(fake_calls));
	fake_call_count = 0U;
	fake_tone_ret = 0;
	fake_stop_ret = 0;
}

ZTEST(buzzer_rttl, test_buzzer_play_rttl_defaults_and_notes)
{
	const char *rttl = "Tune:b=960:c,16d#,8p,8a6.";

	zassert_ok(buzzer_play_rttl(&fake_buzzer, rttl));
	zassert_equal(fake_call_count, 5U);

	zassert_equal(fake_calls[0].type, FAKE_CALL_TONE);
	zassert_equal(fake_calls[0].freq_hz, 1047U);
	zassert_equal(fake_calls[0].duration_ms, 62U);

	zassert_equal(fake_calls[1].type, FAKE_CALL_TONE);
	zassert_equal(fake_calls[1].freq_hz, 1245U);
	zassert_equal(fake_calls[1].duration_ms, 15U);

	zassert_equal(fake_calls[2].type, FAKE_CALL_STOP);

	zassert_equal(fake_calls[3].type, FAKE_CALL_TONE);
	zassert_equal(fake_calls[3].freq_hz, 1760U);
	zassert_equal(fake_calls[3].duration_ms, 46U);

	zassert_equal(fake_calls[4].type, FAKE_CALL_STOP);
}

ZTEST(buzzer_rttl, test_buzzer_play_rttl_invalid_note_stops_buzzer)
{
	zassert_equal(buzzer_play_rttl(&fake_buzzer, "Tune:b=960:h"), -EINVAL);
	zassert_equal(fake_call_count, 1U);
	zassert_equal(fake_calls[0].type, FAKE_CALL_STOP);
}

ZTEST(buzzer_rttl, test_buzzer_play_rttl_device_error_stops_buzzer)
{
	fake_tone_ret = -EIO;

	zassert_equal(buzzer_play_rttl(&fake_buzzer, "Tune:b=960:c"), -EIO);
	zassert_equal(fake_call_count, 2U);
	zassert_equal(fake_calls[0].type, FAKE_CALL_TONE);
	zassert_equal(fake_calls[1].type, FAKE_CALL_STOP);
}

static void *buzzer_rttl_setup(void)
{
	reset_fake_buzzer();
	return NULL;
}

static void buzzer_rttl_before(void *data)
{
	ARG_UNUSED(data);

	reset_fake_buzzer();
}

ZTEST_SUITE(buzzer_rttl, NULL, buzzer_rttl_setup, buzzer_rttl_before, NULL, NULL);
