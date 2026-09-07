/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_input.h"

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "src/aa.pb.h"
#include "aa_display.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"
#include "aa_touch.h"

LOG_MODULE_REGISTER(aa_input, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Input channel: touch events are scaled from the head unit touch screen to
 * the stream resolution and injected as a touchscreen, key presses are mapped
 * to Zephyr input key codes.
 */

static const struct {
	uint16_t android;
	uint16_t zephyr;
} key_map[] = {
	{AA_KEYCODE_HOME, INPUT_KEY_HOME},
	{AA_KEYCODE_BACK, INPUT_KEY_BACK},
	{AA_KEYCODE_DPAD_UP, INPUT_KEY_UP},
	{AA_KEYCODE_DPAD_DOWN, INPUT_KEY_DOWN},
	{AA_KEYCODE_DPAD_LEFT, INPUT_KEY_LEFT},
	{AA_KEYCODE_DPAD_RIGHT, INPUT_KEY_RIGHT},
	{AA_KEYCODE_DPAD_CENTER, INPUT_KEY_ENTER},
	{AA_KEYCODE_VOLUME_UP, INPUT_KEY_VOLUMEUP},
	{AA_KEYCODE_VOLUME_DOWN, INPUT_KEY_VOLUMEDOWN},
	{AA_KEYCODE_MEDIA_PLAY_PAUSE, INPUT_KEY_PLAYPAUSE},
	{AA_KEYCODE_MEDIA_NEXT, INPUT_KEY_NEXTSONG},
	{AA_KEYCODE_MEDIA_PREVIOUS, INPUT_KEY_PREVIOUSSONG},
};

static bool touch_down;
static uint16_t last_x;
static uint16_t last_y;

static InputEventIndication event;

void aa_input_link_up(void)
{
	touch_down = false;
}

void aa_input_link_down(void)
{
	if (touch_down) {
		touch_down = false;
		(void)aa_touch_report(last_x, last_y, false);
	}
}

int aa_input_bind(void)
{
	BindingRequest req = BindingRequest_init_zero;
	uint8_t buf[64];
	int n;

	if (!IS_ENABLED(CONFIG_SAMPLE_AA_INPUT_KEYS)) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(key_map) && i < ARRAY_SIZE(req.scan_codes); i++) {
		req.scan_codes[req.scan_codes_count++] = key_map[i].android;
	}

	n = aa_pb_encode(buf, sizeof(buf), BindingRequest_fields, &req);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(aa_session_get()->input_ch, false, AA_INPUT_BINDING_REQUEST, buf,
			   (size_t)n);
}

static uint16_t scale_coord(uint32_t value, uint16_t from, uint16_t to)
{
	uint32_t scaled = (from != 0U) ? (value * to) / from : value;

	return (uint16_t)MIN(scaled, (uint32_t)to - 1U);
}

static void on_touch(const TouchEvent *touch)
{
	const TouchLocation *loc = &touch->touch_location[0];
	struct aa_session *s = aa_session_get();
	bool pressed;

	if (touch->touch_location_count == 0U) {
		return;
	}

	switch (touch->touch_action) {
	case AA_TOUCH_ACTION_PRESS:
	case AA_TOUCH_ACTION_DRAG:
		pressed = true;
		break;
	case AA_TOUCH_ACTION_RELEASE:
		pressed = false;
		break;
	default:
		/* Additional pointers of a multi-touch gesture are ignored */
		return;
	}

	last_x = scale_coord(loc->x, s->touch_width, AA_VIDEO_WIDTH);
	last_y = scale_coord(loc->y, s->touch_height, AA_VIDEO_HEIGHT);
	touch_down = pressed;

	LOG_DBG("Touch %s at %u,%u", pressed ? "down" : "up", last_x, last_y);
	(void)aa_touch_report(last_x, last_y, pressed);
}

static void on_buttons(const ButtonEvents *buttons)
{
	for (pb_size_t i = 0; i < buttons->button_count; i++) {
		const ButtonEvent *btn = &buttons->button[i];
		bool mapped = false;

		for (size_t k = 0; k < ARRAY_SIZE(key_map); k++) {
			if (key_map[k].android == btn->scan_code) {
				(void)aa_touch_report_key(key_map[k].zephyr, btn->is_pressed);
				mapped = true;
				break;
			}
		}

		LOG_INF("Key %u %s%s", btn->scan_code, btn->is_pressed ? "pressed" : "released",
			mapped ? "" : " (unmapped)");
	}
}

void aa_input_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	switch (msg_id) {
	case AA_INPUT_EVENT_INDICATION:
		if (aa_pb_decode(body, len, InputEventIndication_fields, &event) != 0) {
			return;
		}
		if (event.has_touch_event) {
			on_touch(&event.touch_event);
		}
		if (event.has_button_event && IS_ENABLED(CONFIG_SAMPLE_AA_INPUT_KEYS)) {
			on_buttons(&event.button_event);
		}
		break;

	case AA_INPUT_BINDING_RESPONSE: {
		BindingResponse rsp = BindingResponse_init_zero;

		if (aa_pb_decode(body, len, BindingResponse_fields, &rsp) == 0) {
			LOG_INF("Key binding %s", (rsp.has_status && rsp.status != AA_STATUS_OK)
							  ? "refused"
							  : "accepted");
		}
		break;
	}

	default:
		LOG_WRN("Unhandled input message 0x%04x (%zu bytes)", msg_id, len);
		break;
	}
}
