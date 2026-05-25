/*
 * Copyright 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

static const struct device *const fake_dev =
	DEVICE_DT_GET(DT_NODELABEL(fake_input_device));
static const struct device *const keymap_dev =
	DEVICE_DT_GET(DT_NODELABEL(keymap));

DEVICE_DT_DEFINE(DT_INST(0, vnd_input_device), NULL, NULL, NULL, NULL,
		 PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

static struct input_event events[16];
static int event_count;

static void test_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);
	zassert_true(event_count < ARRAY_SIZE(events));
	memcpy(&events[event_count], evt, sizeof(*evt));
	event_count++;
}
INPUT_CALLBACK_DEFINE(keymap_dev, test_cb, NULL);

static void clear_events(void)
{
	event_count = 0;
	memset(events, 0, sizeof(events));
}

static void report_matrix_event(uint32_t row, uint32_t col, bool pressed)
{
	input_report_abs(fake_dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(fake_dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(fake_dev, INPUT_BTN_TOUCH, pressed, true, K_FOREVER);
}

static void assert_event(int index, uint16_t code, int32_t value)
{
	zassert_true(index < event_count, "missing event %d", index);
	zassert_equal(events[index].type, INPUT_EV_KEY);
	zassert_equal(events[index].code, code);
	zassert_equal(events[index].value, value);
}

ZTEST(input_keymap, test_base_keymap)
{
	clear_events();

	report_matrix_event(0, 0, true);
	report_matrix_event(0, 0, false);

	zassert_equal(event_count, 2);
	assert_event(0, INPUT_KEY_A, 1);
	assert_event(1, INPUT_KEY_A, 0);
}

ZTEST(input_keymap, test_layer_release_uses_pressed_code)
{
	clear_events();

	report_matrix_event(0, 1, true);
	report_matrix_event(0, 0, true);
	report_matrix_event(0, 1, false);
	report_matrix_event(0, 0, false);

	zassert_equal(event_count, 4);
	assert_event(0, INPUT_KEY_LEFTSHIFT, 1);
	assert_event(1, INPUT_KEY_Z, 1);
	assert_event(2, INPUT_KEY_LEFTSHIFT, 0);
	assert_event(3, INPUT_KEY_Z, 0);
}

ZTEST(input_keymap, test_layer_fallback_prefers_most_specific_match)
{
	clear_events();

	report_matrix_event(0, 1, true);
	report_matrix_event(0, 2, true);
	report_matrix_event(0, 0, true);
	report_matrix_event(0, 0, false);
	report_matrix_event(1, 0, true);
	report_matrix_event(1, 0, false);
	report_matrix_event(0, 2, false);
	report_matrix_event(0, 1, false);

	zassert_equal(event_count, 8);
	assert_event(0, INPUT_KEY_LEFTSHIFT, 1);
	assert_event(1, INPUT_KEY_LEFTCTRL, 1);
	assert_event(2, INPUT_KEY_X, 1);
	assert_event(3, INPUT_KEY_X, 0);
	assert_event(4, INPUT_KEY_Y, 1);
	assert_event(5, INPUT_KEY_Y, 0);
	assert_event(6, INPUT_KEY_LEFTCTRL, 0);
	assert_event(7, INPUT_KEY_LEFTSHIFT, 0);
}

ZTEST_SUITE(input_keymap, NULL, NULL, NULL, NULL, NULL);
