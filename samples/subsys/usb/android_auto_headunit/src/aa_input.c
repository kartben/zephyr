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

/* Pointers the message can carry, so also the slots that are tracked */
#define TOUCH_MAX_POINTS ARRAY_SIZE(((TouchEvent *)0)->touch_location)

struct touch_point {
	uint32_t x;
	uint32_t y;
	/* Track id the screen gave this pointer, -1 while the slot is free */
	int32_t id;
	bool down;
};

/*
 * Report every pointer of the gesture, with action_index naming the one the
 * action applies to. A pointer being lifted is still part of the report, which
 * is what the phone's input stack expects.
 */
static int __maybe_unused send_touch(const struct touch_point *pts, uint8_t slot, int action)
{
	InputEventIndication ind = InputEventIndication_init_zero;
	bool lifting = (action == AA_TOUCH_ACTION_RELEASE || action == AA_TOUCH_ACTION_POINTER_UP);
	uint8_t buf[128];
	uint32_t count = 0;
	int n;

	if (atomic_get(&forwarding) == 0) {
		return 0;
	}

	for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (!pts[i].down && !(lifting && i == slot)) {
			continue;
		}

		if (i == slot) {
			ind.touch_event.has_action_index = true;
			ind.touch_event.action_index = count;
		}

		ind.touch_event.touch_location[count].has_x = true;
		ind.touch_event.touch_location[count].x = pts[i].x;
		ind.touch_event.touch_location[count].has_y = true;
		ind.touch_event.touch_location[count].y = pts[i].y;
		ind.touch_event.touch_location[count].has_pointer_id = true;
		ind.touch_event.touch_location[count].pointer_id = i;
		count++;
	}

	if (count == 0U) {
		return 0;
	}

	ind.has_timestamp = true;
	ind.timestamp = (uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
	ind.has_disp_channel = true;
	ind.disp_channel = 0;
	ind.has_touch_event = true;
	ind.touch_event.touch_location_count = count;
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

static struct touch_point points[TOUCH_MAX_POINTS];
static uint8_t cur_slot;
static int64_t last_drag;

/*
 * The screen numbers its pointers with track ids of its own choosing, which do
 * not have to be small or contiguous, so give each one a slot of ours. The slot
 * is what the phone sees as the pointer id, and it lasts until the finger is
 * lifted.
 */
static uint8_t slot_of(int32_t id)
{
	uint8_t free_slot = UINT8_MAX;

	for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (points[i].id == id) {
			return i;
		}

		if (points[i].id < 0 && free_slot == UINT8_MAX) {
			free_slot = i;
		}
	}

	if (free_slot != UINT8_MAX) {
		points[free_slot].id = id;
	}

	return free_slot;
}

static void slots_init(void)
{
	for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
		points[i].id = -1;
		points[i].down = false;
	}
}

static uint8_t points_down(void)
{
	uint8_t n = 0;

	for (uint8_t i = 0; i < TOUCH_MAX_POINTS; i++) {
		if (points[i].down) {
			n++;
		}
	}

	return n;
}

/* Which Android action a change to one pointer amounts to for the gesture */
static void report_slot(uint8_t slot, bool changed)
{
	int action;

	if (!changed) {
		int64_t now = k_uptime_get();

		if (!points[slot].down) {
			/* A pointer that never pressed; give its slot back */
			points[slot].id = -1;
			return;
		}

		if ((now - last_drag) < TOUCH_DRAG_MIN_MS) {
			return;
		}
		last_drag = now;
		action = AA_TOUCH_ACTION_DRAG;
	} else if (points[slot].down) {
		action = (points_down() == 1U) ? AA_TOUCH_ACTION_PRESS
					       : AA_TOUCH_ACTION_POINTER_DOWN;
		last_drag = k_uptime_get();
		LOG_INF("Touch press at %u,%u, pointer %u of %u", points[slot].x, points[slot].y,
			slot, points_down());
	} else {
		action = (points_down() == 0U) ? AA_TOUCH_ACTION_RELEASE
					       : AA_TOUCH_ACTION_POINTER_UP;
		LOG_INF("Touch release at %u,%u, pointer %u, %u left", points[slot].x,
			points[slot].y, slot, points_down());
	}

	(void)send_touch(points, slot, action);

	if (!points[slot].down) {
		points[slot].id = -1;
	}
}

static void touch_cb(struct input_event *evt, void *user_data)
{
	bool changed = false;

	ARG_UNUSED(user_data);

	switch (evt->code) {
	case INPUT_ABS_MT_SLOT:
		/* Carries sync, but arrives ahead of this pointer's position */
		cur_slot = slot_of(evt->value);
		return;
	case INPUT_ABS_X:
	case INPUT_ABS_Y:
	case INPUT_BTN_TOUCH:
		break;
	default:
		return;
	}

	if (cur_slot >= TOUCH_MAX_POINTS) {
		return;
	}

	if (evt->code == INPUT_ABS_X) {
		points[cur_slot].x = (uint32_t)evt->value;
	} else if (evt->code == INPUT_ABS_Y) {
		points[cur_slot].y = (uint32_t)evt->value;
	} else {
		changed = points[cur_slot].down != (evt->value != 0);
		points[cur_slot].down = evt->value != 0;
	}

	if (!evt->sync) {
		return;
	}

	report_slot(cur_slot, changed);
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_CHOSEN(zephyr_touch)), touch_cb, NULL);

#endif /* touch device */

#ifdef CONFIG_SAMPLE_AA_HU_DEMO_TAP

static K_THREAD_STACK_DEFINE(demo_stack, 2048);
static struct k_thread demo_thread_data;

static void demo_thread(void *p1, void *p2, void *p3)
{
	struct touch_point tap[TOUCH_MAX_POINTS] = {
		{ .x = CONFIG_SAMPLE_AA_HU_DEMO_TAP_X, .y = CONFIG_SAMPLE_AA_HU_DEMO_TAP_Y },
	};

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
		tap[0].down = true;
		(void)send_touch(tap, 0U, AA_TOUCH_ACTION_PRESS);
		k_sleep(K_MSEC(120));
		tap[0].down = false;
		(void)send_touch(tap, 0U, AA_TOUCH_ACTION_RELEASE);
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
#if DT_HAS_CHOSEN(zephyr_touch) && DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_touch), okay)
	slots_init();
#endif
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
#if DT_HAS_CHOSEN(zephyr_touch) && DT_NODE_HAS_STATUS(DT_CHOSEN(zephyr_touch), okay)
	slots_init();
#endif
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
