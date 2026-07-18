/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <lvgl.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);

#include "agent_pad.h"

#define OLED_W	 128
#define HEADER_H 14
#define SLOT_W	 20
#define SLOT_H	 26
#define SLOT_Y	 17

#ifdef CONFIG_AGENT_PAD_SIM
lv_obj_t *sim_build(lv_obj_t *root);
#endif

static const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

static lv_obj_t *layer_label;
static lv_obj_t *page_label;
static lv_obj_t *slots[AGENT_SLOTS];
static lv_obj_t *slot_labels[AGENT_SLOTS];
static lv_obj_t *note_label;

static const char *status_text(enum agent_status status, int tick)
{
	static const char *const spinner[] = {"|", "/", "-", "\\"};

	switch (status) {
	case AGENT_IDLE:
		return "-";
	case AGENT_THINKING:
		return spinner[tick % 4];
	case AGENT_DONE:
		return LV_SYMBOL_OK;
	case AGENT_INPUT:
		return LV_SYMBOL_BELL;
	case AGENT_ERROR:
		return LV_SYMBOL_WARNING;
	default:
		return "";
	}
}

static void refresh(int tick)
{
	char note[NOTE_LEN];
	struct agent agent;

	lv_label_set_text(layer_label, layers[state_layer()].name);
	lv_label_set_text_fmt(page_label, "L%d", state_layer() + 1);

	for (int i = 0; i < AGENT_SLOTS; i++) {
		bool alert;

		state_agent_get(i, &agent);
		alert = agent.status == AGENT_INPUT || agent.status == AGENT_ERROR;

		lv_label_set_text(slot_labels[i], status_text(agent.status, tick));
		lv_obj_set_style_bg_opa(slots[i], alert ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
		lv_obj_set_style_text_color(slot_labels[i],
					    alert ? lv_color_black() : lv_color_white(), 0);
		lv_obj_set_style_border_opa(
			slots[i], agent.status == AGENT_EMPTY ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
	}

	state_note_get(note, sizeof(note));
	lv_label_set_text(note_label, note);
}

static lv_obj_t *make_box(lv_obj_t *parent)
{
	lv_obj_t *obj = lv_obj_create(parent);

	lv_obj_remove_style_all(obj);
	return obj;
}

static void build_screen(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *header;

#ifdef CONFIG_AGENT_PAD_SIM
	scr = sim_build(scr);
#endif

	lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(scr, lv_color_white(), 0);

	header = make_box(scr);
	lv_obj_set_pos(header, 0, 0);
	lv_obj_set_size(header, OLED_W, HEADER_H);
	lv_obj_set_style_bg_color(header, lv_color_white(), 0);
	lv_obj_set_style_bg_opa(header, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(header, lv_color_black(), 0);

	layer_label = lv_label_create(header);
	lv_obj_align(layer_label, LV_ALIGN_LEFT_MID, 2, 0);

	page_label = lv_label_create(header);
	lv_obj_align(page_label, LV_ALIGN_RIGHT_MID, -2, 0);

	for (int i = 0; i < AGENT_SLOTS; i++) {
		slots[i] = make_box(scr);
		lv_obj_set_pos(slots[i], 2 + i * (SLOT_W + 1), SLOT_Y);
		lv_obj_set_size(slots[i], SLOT_W, SLOT_H);
		lv_obj_set_style_border_width(slots[i], 1, 0);
		lv_obj_set_style_border_color(slots[i], lv_color_white(), 0);
		lv_obj_set_style_bg_color(slots[i], lv_color_white(), 0);
		lv_obj_set_style_radius(slots[i], 3, 0);

		slot_labels[i] = lv_label_create(slots[i]);
		lv_obj_center(slot_labels[i]);
	}

	note_label = lv_label_create(scr);
	lv_obj_set_pos(note_label, 2, SLOT_Y + SLOT_H + 4);
	lv_obj_set_width(note_label, OLED_W - 4);
	lv_label_set_long_mode(note_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
}

static void ui_thread(void *p1, void *p2, void *p3)
{
	uint32_t seen = ~0U;
	uint32_t iter = 0;
	int tick = 0;

	while (true) {
		struct agent agent;
		bool thinking = false;

		for (int i = 0; i < AGENT_SLOTS; i++) {
			state_agent_get(i, &agent);
			thinking = thinking || (agent.status == AGENT_THINKING);
		}

		if (state_seq() != seen || (thinking && (iter % 4) == 0)) {
			seen = state_seq();
			refresh(tick++);
		}

		lv_timer_handler();
		iter++;
		k_msleep(50);
	}
}

K_THREAD_DEFINE(ui_tid, 4096, ui_thread, NULL, NULL, NULL, 8, 0, K_TICKS_FOREVER);

int ui_init(void)
{
	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return -EIO;
	}

	build_screen();
	state_note_set("agents ready");
	display_blanking_off(display);

	k_thread_start(ui_tid);

	return 0;
}
