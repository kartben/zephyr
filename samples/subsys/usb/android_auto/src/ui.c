/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>

#include <lvgl.h>
#ifdef CONFIG_SAMPLE_AA_DEMO_WIDGETS
#include <lv_demos.h>
#endif

#include "aa_encoder.h"

/*
 * A small touch-interactive screen: a counter button, a slider driving a bar
 * and a night mode switch, plus labels showing the link state and the video
 * statistics. State coming from other threads is stored in atomics and
 * applied from an LVGL timer so that LVGL objects are only touched from the
 * LVGL thread.
 */

#define UI_TIMER_PERIOD_MS 500

static lv_obj_t *link_label;
static lv_obj_t *stats_label;
static lv_obj_t *count_label;
static lv_obj_t *slider_label;
static lv_obj_t *bar;
static lv_obj_t *night_switch;
static lv_obj_t *counter_btn;
static uint32_t tap_count;
static atomic_t link_state_req = ATOMIC_INIT(AA_UI_LINK_DOWN);
static atomic_t night_req = ATOMIC_INIT(0);
static enum aa_ui_link_state link_state_cur = AA_UI_LINK_DOWN;
static bool night_cur;

static const char *const link_state_text[] = {
	[AA_UI_LINK_DOWN] = "No head unit",
	[AA_UI_LINK_CONNECTED] = "Connected",
	[AA_UI_LINK_SECURE] = "Authenticated",
	[AA_UI_LINK_STREAMING] = "Streaming",
};

static void apply_theme(bool night)
{
	lv_theme_t *theme;

	theme = lv_theme_default_init(NULL, lv_palette_main(LV_PALETTE_BLUE),
				      lv_palette_main(LV_PALETTE_RED), night, LV_FONT_DEFAULT);
	lv_display_set_theme(NULL, theme);
	night_cur = night;
	if (night_switch != NULL) {
		if (night) {
			lv_obj_add_state(night_switch, LV_STATE_CHECKED);
		} else {
			lv_obj_remove_state(night_switch, LV_STATE_CHECKED);
		}
	}
}

static void ui_timer_cb(lv_timer_t *timer)
{
	struct aa_video_stats stats;
	enum aa_ui_link_state link_state = (enum aa_ui_link_state)atomic_get(&link_state_req);
	bool night = atomic_get(&night_req) != 0;

	if (link_state != link_state_cur) {
		link_state_cur = link_state;
		if (link_label != NULL) {
			lv_label_set_text(link_label, link_state_text[link_state]);
		}
	}

	if (night != night_cur) {
		apply_theme(night);
	}

	if (stats_label != NULL) {
		aa_encoder_get_stats(&stats);
		lv_label_set_text_fmt(stats_label, "%u.%u fps  %u kbit/s  %u frames  %u pending MB",
				      stats.fps_x10 / 10U, stats.fps_x10 % 10U, stats.kbit_s,
				      stats.frames, stats.pending_mbs);
	}
}

static void counter_event_cb(lv_event_t *e)
{
	ARG_UNUSED(e);
	tap_count++;
	lv_label_set_text_fmt(count_label, "Tap me: %u", tap_count);
}

static void slider_event_cb(lv_event_t *e)
{
	lv_obj_t *slider = lv_event_get_target_obj(e);
	int32_t value = lv_slider_get_value(slider);

	lv_bar_set_value(bar, value, LV_ANIM_OFF);
	lv_label_set_text_fmt(slider_label, "%d %%", (int)value);
}

static void night_switch_cb(lv_event_t *e)
{
	lv_obj_t *sw = lv_event_get_target_obj(e);

	aa_ui_set_night(lv_obj_has_state(sw, LV_STATE_CHECKED));
}

static lv_obj_t *row_create(lv_obj_t *parent)
{
	lv_obj_t *row = lv_obj_create(parent);

	lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
	lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

	return row;
}

int aa_ui_init(void)
{
	lv_obj_t *scr = lv_screen_active();
	lv_obj_t *header;
	lv_obj_t *title;
	lv_obj_t *btn;
	lv_obj_t *slider;
	lv_obj_t *row;
	lv_obj_t *label;

#ifdef CONFIG_SAMPLE_AA_DEMO_WIDGETS
	lv_demo_widgets();
	lv_timer_create(ui_timer_cb, UI_TIMER_PERIOD_MS, NULL);
	return 0;
#endif

	lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(scr, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_style_pad_all(scr, 8, 0);

	header = row_create(scr);
	title = lv_label_create(header);
	lv_label_set_text(title, "Zephyr");
#ifdef CONFIG_LV_FONT_MONTSERRAT_24
	if (lv_display_get_horizontal_resolution(NULL) >= 800) {
		lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
	}
#endif
	link_label = lv_label_create(header);
	lv_label_set_text(link_label, link_state_text[AA_UI_LINK_DOWN]);

	btn = lv_button_create(scr);
	lv_obj_set_width(btn, lv_pct(60));
	lv_obj_add_event_cb(btn, counter_event_cb, LV_EVENT_CLICKED, NULL);
	counter_btn = btn;
	count_label = lv_label_create(btn);
	lv_label_set_text(count_label, "Tap me: 0");
	lv_obj_center(count_label);

	slider = lv_slider_create(scr);
	lv_obj_set_width(slider, lv_pct(80));
	lv_slider_set_range(slider, 0, 100);
	lv_slider_set_value(slider, 40, LV_ANIM_OFF);
	lv_obj_add_event_cb(slider, slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

	row = row_create(scr);
	bar = lv_bar_create(row);
	lv_obj_set_width(bar, lv_pct(70));
	lv_bar_set_range(bar, 0, 100);
	lv_bar_set_value(bar, 40, LV_ANIM_OFF);
	slider_label = lv_label_create(row);
	lv_label_set_text(slider_label, "40 %");

	row = row_create(scr);
	label = lv_label_create(row);
	lv_label_set_text(label, "Night mode");
	night_switch = lv_switch_create(row);
	lv_obj_add_event_cb(night_switch, night_switch_cb, LV_EVENT_VALUE_CHANGED, NULL);

	stats_label = lv_label_create(scr);
	lv_label_set_text(stats_label, "Waiting for a head unit");

	lv_timer_create(ui_timer_cb, UI_TIMER_PERIOD_MS, NULL);

	return 0;
}

void aa_ui_set_link_state(enum aa_ui_link_state state)
{
	atomic_set(&link_state_req, (atomic_val_t)state);
}

void aa_ui_set_night(bool night)
{
	atomic_set(&night_req, night ? 1 : 0);
}

int aa_ui_get_tap_target(uint16_t *x, uint16_t *y)
{
	lv_area_t area;

	if (counter_btn == NULL) {
		return -ENOENT;
	}

	lv_obj_update_layout(counter_btn);
	lv_obj_get_coords(counter_btn, &area);
	*x = (uint16_t)((area.x1 + area.x2) / 2);
	*y = (uint16_t)((area.y1 + area.y2) / 2);

	return 0;
}
