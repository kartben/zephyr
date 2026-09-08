/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_input.h"

#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"

LOG_MODULE_REGISTER(aa_input, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Input channel, head unit side: the head unit's own touch screen is forwarded
 * to the phone as touch events, and the phone's key binding request is
 * answered. On native_sim the SDL window's mouse acts as the touch screen; the
 * optional demo tap exercises the channel without a physical screen.
 */

/* Forwarding is enabled once the phone has bound the input channel */
static atomic_t forwarding;

static int __maybe_unused send_touch(uint32_t x, uint32_t y, int action)
{
	InputEventIndication ind = InputEventIndication_init_zero;
	uint8_t buf[64];
	int n;

	if (atomic_get(&forwarding) == 0) {
		return 0;
	}

	ind.has_timestamp = true;
	ind.timestamp = (uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
	ind.has_disp_channel = true;
	ind.disp_channel = 0;
	ind.has_touch_event = true;
	ind.touch_event.touch_location_count = 1;
	ind.touch_event.touch_location[0].has_x = true;
	ind.touch_event.touch_location[0].x = x;
	ind.touch_event.touch_location[0].has_y = true;
	ind.touch_event.touch_location[0].y = y;
	ind.touch_event.touch_location[0].has_pointer_id = true;
	ind.touch_event.touch_location[0].pointer_id = 0;
	ind.touch_event.has_touch_action = true;
	ind.touch_event.touch_action = action;

	n = aa_pb_encode(buf, sizeof(buf), InputEventIndication_fields, &ind);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(aa_hu_session_get()->input_ch, false, AA_INPUT_EVENT_INDICATION, buf,
			   (size_t)n);
}

#if DT_HAS_CHOSEN(zephyr_touch) && DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_touch), okay)

/* The screen reports a held finger far faster than the phone needs it */
#define TOUCH_DRAG_MIN_MS 20

static uint32_t cur_x;
static uint32_t cur_y;
static bool cur_pressed;
static bool was_pressed;
static int64_t last_drag;

static void touch_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	switch (evt->code) {
	case INPUT_ABS_X:
		cur_x = (uint32_t)evt->value;
		break;
	case INPUT_ABS_Y:
		cur_y = (uint32_t)evt->value;
		break;
	case INPUT_BTN_TOUCH:
		cur_pressed = evt->value != 0;
		break;
	default:
		return;
	}

	if (!evt->sync) {
		return;
	}

	if (cur_pressed && !was_pressed) {
		LOG_INF("Touch press at %u,%u%s", cur_x, cur_y,
			(atomic_get(&forwarding) != 0) ? "" : " (not forwarded)");
		last_drag = k_uptime_get();
		(void)send_touch(cur_x, cur_y, AA_TOUCH_ACTION_PRESS);
	} else if (cur_pressed && was_pressed) {
		int64_t now = k_uptime_get();

		if ((now - last_drag) < TOUCH_DRAG_MIN_MS) {
			return;
		}
		last_drag = now;
		LOG_DBG("Touch drag to %u,%u", cur_x, cur_y);
		(void)send_touch(cur_x, cur_y, AA_TOUCH_ACTION_DRAG);
	} else if (!cur_pressed && was_pressed) {
		LOG_INF("Touch release at %u,%u", cur_x, cur_y);
		(void)send_touch(cur_x, cur_y, AA_TOUCH_ACTION_RELEASE);
	}
	was_pressed = cur_pressed;
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_cb, NULL);

#endif /* touch device */

#ifdef CONFIG_SAMPLE_AA_HU_DEMO_TAP

static K_THREAD_STACK_DEFINE(demo_stack, 2048);
static struct k_thread demo_thread_data;

static void demo_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		k_sleep(K_MSEC(CONFIG_SAMPLE_AA_HU_DEMO_TAP_INTERVAL_MS));
		if (atomic_get(&forwarding) == 0) {
			continue;
		}
		LOG_INF("Demo tap at %u,%u", (uint32_t)CONFIG_SAMPLE_AA_HU_DEMO_TAP_X,
			(uint32_t)CONFIG_SAMPLE_AA_HU_DEMO_TAP_Y);
		(void)send_touch(CONFIG_SAMPLE_AA_HU_DEMO_TAP_X, CONFIG_SAMPLE_AA_HU_DEMO_TAP_Y,
				 AA_TOUCH_ACTION_PRESS);
		k_sleep(K_MSEC(120));
		(void)send_touch(CONFIG_SAMPLE_AA_HU_DEMO_TAP_X, CONFIG_SAMPLE_AA_HU_DEMO_TAP_Y,
				 AA_TOUCH_ACTION_RELEASE);
	}
}

static void demo_start(void)
{
	k_thread_create(&demo_thread_data, demo_stack, K_THREAD_STACK_SIZEOF(demo_stack),
			demo_thread, NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_RX_THREAD_PRIORITY + 1,
			0, K_NO_WAIT);
	k_thread_name_set(&demo_thread_data, "aa_hu_demo");
}

#else
static void demo_start(void)
{
}
#endif /* CONFIG_SAMPLE_AA_HU_DEMO_TAP */

int aa_input_init(void)
{
	demo_start();

	return 0;
}

void aa_input_link_up(void)
{
	atomic_set(&forwarding, 0);
}

void aa_input_link_down(void)
{
	atomic_set(&forwarding, 0);
}

void aa_input_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	if (msg_id == AA_INPUT_BINDING_REQUEST) {
		BindingResponse rsp = BindingResponse_init_zero;
		uint8_t buf[8];
		int n;

		ARG_UNUSED(body);
		ARG_UNUSED(len);
		rsp.has_status = true;
		rsp.status = AA_STATUS_OK;
		n = aa_pb_encode(buf, sizeof(buf), BindingResponse_fields, &rsp);
		if (n >= 0) {
			(void)aa_msg_send(aa_hu_session_get()->input_ch, false,
					  AA_INPUT_BINDING_RESPONSE, buf, (size_t)n);
		}

		/* The channel is bound: start forwarding touch events */
		atomic_set(&forwarding, 1);
		LOG_INF("Input channel bound, forwarding touch");
	} else {
		LOG_WRN("Unhandled input message 0x%04x", msg_id);
	}
}
