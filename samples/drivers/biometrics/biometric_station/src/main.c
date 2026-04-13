/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/biometrics_dfrobot_ai10.h>
#include <zephyr/drivers/biometrics/emul.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <lvgl.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define BIOMETRICS_NODE DT_ALIAS(biometrics)

#if !DT_NODE_EXISTS(BIOMETRICS_NODE)
#error "Devicetree alias 'biometrics' must point at a biometric device"
#endif

#define ENROLL_TEMPLATE_ID 1U
#define SPLASH_TIME_MS     1200U
#define PROMPT_TIME_MS     1500U
#define WORKER_PRIORITY    8
#define WORKER_STACK_SIZE  4096
#define UI_QUEUE_LEN       8
#define UI_CARD_WIDTH_PCT  72
#define UI_PROGRESS_SCALE  100
#define RESULT_TECH_COLOR  0x8ea0b5

enum emul_behavior_mode {
	EMUL_BEHAVIOR_SUCCESS = 0,
	EMUL_BEHAVIOR_NO_MATCH,
	EMUL_BEHAVIOR_TIMEOUT,
};

#define APP_EMULATOR_BEHAVIOR EMUL_BEHAVIOR_SUCCESS

enum app_state {
	APP_STATE_SPLASH = 0,
	APP_STATE_ENROLLING,
	APP_STATE_PROMPT,
	APP_STATE_SCANNING,
	APP_STATE_RESULT,
};

enum worker_command_type {
	WORKER_CMD_ENROLL = 1,
	WORKER_CMD_IDENTIFY,
};

enum ui_event_type {
	UI_EVT_ENROLL_PROGRESS = 1,
	UI_EVT_ENROLL_DONE,
	UI_EVT_IDENTIFY_DONE,
};

struct worker_command {
	uint8_t type;
};

struct ui_event {
	uint8_t type;
	int32_t result;
	bool has_user_info;
	union {
		struct biometric_capture_result capture;
		struct biometric_match_result match;
	} data;
	struct dfrobot_ai10_user_info user_info;
};

K_MSGQ_DEFINE(worker_cmd_msgq, sizeof(struct worker_command), 4, 4);
K_MSGQ_DEFINE(ui_evt_msgq, sizeof(struct ui_event), UI_QUEUE_LEN, 4);
K_THREAD_STACK_DEFINE(worker_stack, WORKER_STACK_SIZE);

static const struct device *const biometric = DEVICE_DT_GET(BIOMETRICS_NODE);
static const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#if DT_HAS_CHOSEN(zephyr_console)
static const struct device *const console_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
#else
static const struct device *const console_dev;
#endif

static struct k_thread worker_thread;

static enum app_state current_state;
static uint32_t state_deadline_ms;
static bool identify_requested;

static lv_obj_t *screen;
static lv_obj_t *ambient_top;
static lv_obj_t *ambient_bottom;
static lv_obj_t *card;
static lv_obj_t *badge;
static lv_obj_t *title_label;
static lv_obj_t *subtitle_label;
static lv_obj_t *hero_ring;
static lv_obj_t *success_icon;
static lv_obj_t *busy_spinner;
static lv_obj_t *progress_bar;
static lv_obj_t *metrics_label;
static lv_obj_t *footer_label;
static lv_obj_t *stage_row;
static lv_obj_t *stage_label[4];

static void set_state(enum app_state state, uint32_t duration_ms)
{
	current_state = state;
	state_deadline_ms = duration_ms == 0U ? 0U : k_uptime_get_32() + duration_ms;
}

static bool is_emulator(void)
{
	return DT_NODE_HAS_COMPAT(BIOMETRICS_NODE, zephyr_biometrics_emul);
}

static bool enrollment_enabled(void)
{
	return IS_ENABLED(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL);
}

static void queue_worker_command(enum worker_command_type type)
{
	const struct worker_command cmd = {
		.type = type,
	};

	if (k_msgq_put(&worker_cmd_msgq, &cmd, K_NO_WAIT) != 0) {
		LOG_ERR("Worker command queue full");
	}
}

static void queue_ui_event(const struct ui_event *evt)
{
	if (k_msgq_put(&ui_evt_msgq, evt, K_MSEC(250)) != 0) {
		LOG_ERR("UI event queue full");
	}
}

static bool templates_present(void)
{
	uint16_t ids[4];
	size_t count = 0;
	int ret;

	ret = biometric_template_list(biometric, ids, ARRAY_SIZE(ids), &count);
	if (ret < 0) {
		LOG_ERR("biometric_template_list failed: %d", ret);
		return false;
	}

	return count > 0U;
}

static void maybe_configure_identify_behavior(void)
{
	if (!is_emulator()) {
		return;
	}

	biometrics_emul_set_match_fail(biometric, false);
	biometrics_emul_set_capture_timeout(biometric, false);
	biometrics_emul_set_match_score(biometric, 92);
	biometrics_emul_set_image_quality(biometric, 88);
	biometrics_emul_set_match_id(biometric, ENROLL_TEMPLATE_ID);

	if (APP_EMULATOR_BEHAVIOR == EMUL_BEHAVIOR_NO_MATCH) {
		biometrics_emul_set_match_fail(biometric, true);
	} else if (APP_EMULATOR_BEHAVIOR == EMUL_BEHAVIOR_TIMEOUT) {
		biometrics_emul_set_capture_timeout(biometric, true);
	}
}

static void style_root(lv_obj_t *obj)
{
	lv_obj_set_style_bg_color(obj, lv_color_hex(0x0d1321), 0);
	lv_obj_set_style_bg_grad_color(obj, lv_color_hex(0x1d2d44), 0);
	lv_obj_set_style_bg_grad_dir(obj, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_panel(lv_obj_t *obj, lv_color_t color, lv_opa_t opa)
{
	lv_obj_set_style_radius(obj, 32, 0);
	lv_obj_set_style_bg_color(obj, color, 0);
	lv_obj_set_style_bg_opa(obj, opa, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
}

static void style_card(void)
{
	lv_obj_set_style_radius(card, 30, 0);
	lv_obj_set_style_bg_color(card, lv_color_hex(0x132238), 0);
	lv_obj_set_style_bg_grad_color(card, lv_color_hex(0x0f1b2d), 0);
	lv_obj_set_style_bg_grad_dir(card, LV_GRAD_DIR_VER, 0);
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_border_color(card, lv_color_hex(0x2b4162), 0);
	lv_obj_set_style_border_width(card, 1, 0);
	lv_obj_set_style_pad_top(card, 34, 0);
	lv_obj_set_style_pad_bottom(card, 34, 0);
	lv_obj_set_style_pad_left(card, 36, 0);
	lv_obj_set_style_pad_right(card, 36, 0);
	lv_obj_set_style_pad_row(card, 16, 0);
}

static void build_stage_row(void)
{
	static const char *const labels_with_enroll[] = {"Intro", "Enroll", "Scan", "Result"};
	static const char *const labels_identify_only[] = {"Intro", "", "Scan", "Result"};
	const char *const *labels =
		enrollment_enabled() ? labels_with_enroll : labels_identify_only;

	stage_row = lv_obj_create(card);
	lv_obj_set_width(stage_row, LV_PCT(100));
	lv_obj_set_height(stage_row, LV_SIZE_CONTENT);
	lv_obj_set_style_bg_opa(stage_row, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(stage_row, 0, 0);
	lv_obj_set_style_pad_all(stage_row, 0, 0);
	lv_obj_set_style_pad_column(stage_row, 10, 0);
	lv_obj_set_flex_flow(stage_row, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(stage_row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	for (size_t i = 0; i < ARRAY_SIZE(stage_label); i++) {
		stage_label[i] = lv_label_create(stage_row);
		lv_label_set_text(stage_label[i], labels[i]);
		lv_obj_set_style_text_font(stage_label[i], &lv_font_montserrat_14, 0);

		if (!enrollment_enabled() && i == APP_STATE_ENROLLING) {
			lv_obj_add_flag(stage_label[i], LV_OBJ_FLAG_HIDDEN);
		}
	}
}

static void build_ui(void)
{
	screen = lv_obj_create(NULL);
	style_root(screen);

	ambient_top = lv_obj_create(screen);
	style_panel(ambient_top, lv_color_hex(0x2ec4b6), LV_OPA_20);
	lv_obj_set_size(ambient_top, 240, 140);
	lv_obj_align(ambient_top, LV_ALIGN_TOP_LEFT, 60, 50);

	ambient_bottom = lv_obj_create(screen);
	style_panel(ambient_bottom, lv_color_hex(0xff9f1c), LV_OPA_20);
	lv_obj_set_size(ambient_bottom, 200, 120);
	lv_obj_align(ambient_bottom, LV_ALIGN_BOTTOM_RIGHT, -70, -60);

	card = lv_obj_create(screen);
	style_card();
	lv_obj_set_width(card, LV_PCT(UI_CARD_WIDTH_PCT));
	lv_obj_set_height(card, LV_PCT(72));
	lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);

	badge = lv_label_create(card);
	lv_label_set_text(badge, "BIOMETRIC STATION");
	lv_obj_set_style_text_font(badge, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_letter_space(badge, 2, 0);
	lv_obj_set_style_text_color(badge, lv_color_hex(0x9ad1d4), 0);

	title_label = lv_label_create(card);
	lv_obj_set_width(title_label, LV_PCT(100));
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(0xf0f4f8), 0);
	lv_obj_set_style_text_align(title_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_WRAP);

	subtitle_label = lv_label_create(card);
	lv_obj_set_width(subtitle_label, LV_PCT(100));
	lv_obj_set_style_text_font(subtitle_label, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(subtitle_label, lv_color_hex(0xb6c2cf), 0);
	lv_obj_set_style_text_align(subtitle_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(subtitle_label, LV_LABEL_LONG_WRAP);

	hero_ring = lv_arc_create(card);
	lv_obj_set_size(hero_ring, 170, 170);
	lv_arc_set_rotation(hero_ring, 120);
	lv_arc_set_bg_angles(hero_ring, 0, 300);
	lv_arc_set_range(hero_ring, 0, UI_PROGRESS_SCALE);
	lv_arc_set_value(hero_ring, 35);
	lv_obj_remove_style(hero_ring, NULL, LV_PART_KNOB);
	lv_obj_set_style_arc_width(hero_ring, 14, LV_PART_MAIN);
	lv_obj_set_style_arc_color(hero_ring, lv_color_hex(0x31445a), LV_PART_MAIN);
	lv_obj_set_style_arc_width(hero_ring, 14, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(hero_ring, lv_color_hex(0x2ec4b6), LV_PART_INDICATOR);
	lv_obj_set_style_pad_all(hero_ring, 8, 0);

	success_icon = lv_label_create(card);
	lv_label_set_text(success_icon, LV_SYMBOL_OK);
	lv_obj_set_style_text_font(success_icon, &lv_font_montserrat_48, 0);
	lv_obj_set_style_text_color(success_icon, lv_color_hex(0x22c55e), 0);
	lv_obj_set_style_text_align(success_icon, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_add_flag(success_icon, LV_OBJ_FLAG_HIDDEN);

	busy_spinner = lv_spinner_create(card);
	lv_obj_set_size(busy_spinner, 110, 110);
	lv_obj_center(busy_spinner);
	lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);

	progress_bar = lv_bar_create(card);
	lv_obj_set_width(progress_bar, LV_PCT(100));
	lv_obj_set_height(progress_bar, 14);
	lv_bar_set_range(progress_bar, 0, UI_PROGRESS_SCALE);
	lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x203246), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(progress_bar, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_radius(progress_bar, 12, LV_PART_MAIN);
	lv_obj_set_style_bg_color(progress_bar, lv_color_hex(0x2ec4b6), LV_PART_INDICATOR);
	lv_obj_set_style_radius(progress_bar, 12, LV_PART_INDICATOR);

	metrics_label = lv_label_create(card);
	lv_obj_set_width(metrics_label, LV_PCT(100));
	lv_obj_set_style_text_font(metrics_label, &lv_font_unscii_8, 0);
	lv_obj_set_style_text_color(metrics_label, lv_color_hex(RESULT_TECH_COLOR), 0);
	lv_obj_set_style_text_align(metrics_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(metrics_label, LV_LABEL_LONG_WRAP);

	build_stage_row();

	footer_label = lv_label_create(card);
	lv_obj_set_width(footer_label, LV_PCT(100));
	lv_obj_set_style_text_font(footer_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(footer_label, lv_color_hex(0x7d8ca1), 0);
	lv_obj_set_style_text_align(footer_label, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(footer_label, LV_LABEL_LONG_WRAP);

	lv_screen_load(screen);
}

static void update_stage_labels(enum app_state state)
{
	static const lv_color_t inactive = LV_COLOR_MAKE(0x89, 0x98, 0xaa);
	static const lv_color_t active = LV_COLOR_MAKE(0xf0, 0xf4, 0xf8);
	static const lv_color_t done = LV_COLOR_MAKE(0x2e, 0xc4, 0xb6);

	for (size_t i = 0; i < ARRAY_SIZE(stage_label); i++) {
		lv_color_t color = inactive;

		if (!enrollment_enabled() && i == APP_STATE_ENROLLING) {
			continue;
		}

		if (state == APP_STATE_RESULT) {
			color = (i < 3U) ? done : active;
		} else if (i < (size_t)state) {
			color = done;
		} else if (i == (size_t)state) {
			color = active;
		}

		lv_obj_set_style_text_color(stage_label[i], color, 0);
	}
}

static void set_ring_visual(lv_color_t color, int value)
{
	lv_arc_set_value(hero_ring, CLAMP(value, 0, UI_PROGRESS_SCALE));
	lv_obj_set_style_arc_color(hero_ring, color, LV_PART_INDICATOR);
}

static void show_busy_spinner(bool show)
{
	if (show) {
		lv_obj_clear_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
	}
}

static void show_success_icon(bool show)
{
	if (show) {
		lv_obj_clear_flag(success_icon, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(success_icon, LV_OBJ_FLAG_HIDDEN);
	}
}

static void show_hero_ring(bool show)
{
	if (show) {
		lv_obj_clear_flag(hero_ring, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(hero_ring, LV_OBJ_FLAG_HIDDEN);
	}
}

static void show_progress_bar(bool show)
{
	if (show) {
		lv_obj_clear_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(progress_bar, LV_OBJ_FLAG_HIDDEN);
	}
}

static void show_stage_row(bool show)
{
	if (show) {
		lv_obj_clear_flag(stage_row, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(stage_row, LV_OBJ_FLAG_HIDDEN);
	}
}

static void render_splash_state(void)
{
	set_state(APP_STATE_SPLASH, SPLASH_TIME_MS);
	update_stage_labels(APP_STATE_SPLASH);
	set_ring_visual(lv_color_hex(0xff9f1c), 18);
	show_busy_spinner(false);
	show_success_icon(false);
	show_hero_ring(true);
	show_progress_bar(false);
	show_stage_row(true);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_label_set_text(title_label, "Initializing station");
	lv_label_set_text(subtitle_label, "Preparing the display pipeline and biometric session.");
	lv_label_set_text(metrics_label, "Resolution 1024 x 768");
	lv_label_set_text(footer_label, "qemu_x86 RAM framebuffer with generic biometrics API");
}

static void render_console_gate_state(void)
{
	set_state(APP_STATE_SPLASH, 0);
	update_stage_labels(APP_STATE_SPLASH);
	set_ring_visual(lv_color_hex(0x5bc0eb), 12);
	show_busy_spinner(false);
	show_success_icon(false);
	show_hero_ring(true);
	show_progress_bar(false);
	show_stage_row(false);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_label_set_text(title_label, "Station armed");
	lv_label_set_text(subtitle_label,
			  "Press any key on the Zephyr console to start the biometric flow.");
	lv_label_set_text(metrics_label, "console gate enabled");
	lv_label_set_text(footer_label,
			  "This keeps the demo idle until the sensor setup is ready.");
}

static void wait_for_console_key(void)
{
	unsigned char ch;

	if (console_dev == NULL || !device_is_ready(console_dev)) {
		LOG_WRN("Console device unavailable, starting immediately");
		return;
	}

	/* Drop any pending bytes so a previous terminal session does not auto-start the demo. */
	while (uart_poll_in(console_dev, &ch) == 0) {
	}

	LOG_INF("Press any key on %s to start the station", console_dev->name);

	while (uart_poll_in(console_dev, &ch) != 0) {
		k_msleep(MIN(lv_timer_handler(), 10U));
	}

	LOG_INF("Startup key received: 0x%02x", ch);
}

#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
static void render_enroll_idle_state(void)
{
	set_state(APP_STATE_ENROLLING, 0);
	update_stage_labels(APP_STATE_ENROLLING);
	set_ring_visual(lv_color_hex(0x2ec4b6), 8);
	show_busy_spinner(false);
	show_success_icon(false);
	show_hero_ring(true);
	show_progress_bar(true);
	show_stage_row(true);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_label_set_text(title_label, "Registering identity");
	lv_label_set_text(subtitle_label,
			  "No template is stored yet. Enrollment starts automatically.");
	lv_label_set_text(metrics_label, "Waiting for biometric samples");
	lv_label_set_text(footer_label, "The worker thread is capturing enrollment data now.");
}
#endif

static void render_prompt_state(void)
{
	set_state(APP_STATE_PROMPT, PROMPT_TIME_MS);
	identify_requested = false;
	update_stage_labels(APP_STATE_PROMPT);
	set_ring_visual(lv_color_hex(0x5bc0eb), 64);
	show_busy_spinner(false);
	show_success_icon(false);
	show_hero_ring(true);
	show_progress_bar(true);
	show_stage_row(enrollment_enabled());
	lv_bar_set_value(progress_bar, UI_PROGRESS_SCALE, LV_ANIM_ON);
	lv_label_set_text(title_label, "Identify yourself");
	lv_label_set_text(subtitle_label,
			  "Hold steady for the next scan. The station will begin automatically.");
	lv_label_set_text(metrics_label, "profile ready");
	lv_label_set_text(footer_label, "Starting identification shortly.");
}

static void render_scanning_state(void)
{
	set_state(APP_STATE_SCANNING, 0);
	update_stage_labels(APP_STATE_SCANNING);
	set_ring_visual(lv_color_hex(0xff9f1c), 82);
	show_busy_spinner(true);
	show_success_icon(false);
	show_hero_ring(false);
	show_progress_bar(false);
	show_stage_row(false);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_label_set_text(title_label, "Scanning");
	lv_label_set_text(subtitle_label, "Comparing live capture against the enrolled profile.");
	lv_label_set_text(metrics_label, "Authentication in progress");
	lv_label_set_text(footer_label, "Waiting for the match result.");
}

static void render_result_success(const struct ui_event *evt)
{
	const struct biometric_match_result *match = &evt->data.match;
	char metrics[128];
	const char *name = (evt->has_user_info && evt->user_info.user_name[0] != '\0')
				   ? evt->user_info.user_name
				   : "recognized user";

	set_state(APP_STATE_RESULT, 0);
	update_stage_labels(APP_STATE_RESULT);
	set_ring_visual(lv_color_hex(0x2ec4b6), UI_PROGRESS_SCALE);
	show_busy_spinner(false);
	show_success_icon(true);
	show_hero_ring(false);
	show_progress_bar(false);
	show_stage_row(false);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);
	lv_label_set_text(title_label, "Welcome back");
	lv_label_set_text(subtitle_label, name);
	snprintk(metrics, sizeof(metrics), "uid:%u / confidence:%d / quality:%u",
		 match->template_id, match->confidence, match->image_quality);
	lv_label_set_text(metrics_label, metrics);
	lv_label_set_text(footer_label, "Identity confirmed. The station remains on this result.");
}

static void render_result_failure(int32_t result)
{
	lv_color_t ring_color;
	const char *title;
	const char *subtitle;

	set_state(APP_STATE_RESULT, 0);
	update_stage_labels(APP_STATE_RESULT);
	show_busy_spinner(false);
	show_success_icon(false);
	show_hero_ring(true);
	show_progress_bar(false);
	show_stage_row(false);
	lv_bar_set_value(progress_bar, 0, LV_ANIM_OFF);

	if (result == -ENOENT) {
		ring_color = lv_color_hex(0xe76f51);
		title = "Not recognized";
		subtitle = "No stored identity matched the captured biometric data.";
	} else if (result == -ETIMEDOUT) {
		ring_color = lv_color_hex(0xffb703);
		title = "Timed out";
		subtitle = "The sensor did not complete the scan before the timeout expired.";
	} else {
		ring_color = lv_color_hex(0xe63946);
		title = "Error";
		subtitle = "The biometric operation failed before a usable result was produced.";
	}

	set_ring_visual(ring_color, 100);
	lv_label_set_text(title_label, title);
	lv_label_set_text(subtitle_label, subtitle);
	lv_label_set_text(metrics_label, "status: held for inspection");
	lv_label_set_text(footer_label,
			  "Check sensor configuration or emulator behavior and rerun.");
}

#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
static void handle_enroll_progress(const struct biometric_capture_result *capture)
{
	char detail[96];
	int progress;

	progress = (capture->samples_captured * UI_PROGRESS_SCALE) / capture->samples_required;
	set_ring_visual(lv_color_hex(0x2ec4b6), progress);
	lv_bar_set_value(progress_bar, progress, LV_ANIM_ON);
	snprintk(detail, sizeof(detail), "Sample %u/%u captured   Quality %u",
		 capture->samples_captured, capture->samples_required, capture->quality);
	lv_label_set_text(metrics_label, detail);

	if (capture->samples_captured < capture->samples_required) {
		lv_label_set_text(subtitle_label,
				  "Enrollment is underway. Additional samples are still needed.");
		lv_label_set_text(footer_label, "The next sample is captured automatically.");
	} else {
		lv_label_set_text(subtitle_label, "Enrollment data complete. Finalizing template.");
		lv_label_set_text(footer_label,
				  "Writing the new identity to the biometrics device.");
	}
}
#endif

static void process_ui_event(const struct ui_event *evt)
{
	switch (evt->type) {
#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
	case UI_EVT_ENROLL_PROGRESS:
		handle_enroll_progress(&evt->data.capture);
		break;
	case UI_EVT_ENROLL_DONE:
		if (evt->result == 0) {
			render_prompt_state();
		} else {
			render_result_failure(evt->result);
		}
		break;
#endif
	case UI_EVT_IDENTIFY_DONE:
		if (evt->result == 0) {
			render_result_success(evt);
		} else {
			render_result_failure(evt->result);
		}
		break;
	default:
		break;
	}
}

#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
static void worker_run_enroll(void)
{
	struct biometric_capabilities capabilities;
	struct ui_event evt = {0};
	int ret;

	ret = biometric_get_capabilities(biometric, &capabilities);
	if (ret < 0) {
		evt.type = UI_EVT_ENROLL_DONE;
		evt.result = ret;
		queue_ui_event(&evt);
		return;
	}

	ret = biometric_enroll_start(biometric, ENROLL_TEMPLATE_ID);
	if (ret < 0) {
		evt.type = UI_EVT_ENROLL_DONE;
		evt.result = ret;
		queue_ui_event(&evt);
		return;
	}

	while (true) {
		struct biometric_capture_result capture = {0};

		ret = biometric_enroll_capture(biometric, K_SECONDS(20), &capture);
		if (ret < 0) {
			(void)biometric_enroll_abort(biometric);
			evt.type = UI_EVT_ENROLL_DONE;
			evt.result = ret;
			queue_ui_event(&evt);
			return;
		}

		evt.type = UI_EVT_ENROLL_PROGRESS;
		evt.result = 0;
		evt.data.capture = capture;
		queue_ui_event(&evt);

		if (capture.samples_captured >= capabilities.enrollment_samples_required) {
			break;
		}
	}

	evt.type = UI_EVT_ENROLL_DONE;
	evt.result = biometric_enroll_finalize(biometric);
	queue_ui_event(&evt);
}
#endif

static void worker_run_identify(void)
{
	struct ui_event evt = {0};

	maybe_configure_identify_behavior();
	evt.type = UI_EVT_IDENTIFY_DONE;
	evt.result = biometric_match(biometric, BIOMETRIC_MATCH_IDENTIFY, 0, K_SECONDS(40),
				     &evt.data.match);
	if (evt.result == 0 && dfrobot_ai10_is_ready(biometric)) {
		if (dfrobot_ai10_user_info_get(biometric, evt.data.match.template_id,
					       &evt.user_info) == 0) {
			evt.has_user_info = true;
		}
	}
	queue_ui_event(&evt);
}

static void worker_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct worker_command cmd;

		k_msgq_get(&worker_cmd_msgq, &cmd, K_FOREVER);

		switch (cmd.type) {
#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
		case WORKER_CMD_ENROLL:
			worker_run_enroll();
			break;
#endif
		case WORKER_CMD_IDENTIFY:
			worker_run_identify();
			break;
		default:
			break;
		}
	}
}

static void step_state_machine(void)
{
	if (state_deadline_ms == 0U || k_uptime_get_32() < state_deadline_ms) {
		return;
	}

	state_deadline_ms = 0U;

	switch (current_state) {
	case APP_STATE_SPLASH:
		if (!enrollment_enabled() || templates_present()) {
			render_prompt_state();
		}
#if defined(CONFIG_BIOMETRIC_STATION_ENABLE_ENROLL)
		else {
			render_enroll_idle_state();
			queue_worker_command(WORKER_CMD_ENROLL);
		}
#endif
		break;
	case APP_STATE_PROMPT:
		if (!identify_requested) {
			identify_requested = true;
			render_scanning_state();
			queue_worker_command(WORKER_CMD_IDENTIFY);
		}
		break;
	default:
		break;
	}
}

int main(void)
{
	int ret;
	struct ui_event evt;

	if (!device_is_ready(biometric)) {
		LOG_ERR("Biometrics device not ready");
		return 0;
	}

	if (!device_is_ready(display)) {
		LOG_ERR("Display device not ready");
		return 0;
	}

	build_ui();
	render_console_gate_state();

	ret = display_blanking_off(display);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("display_blanking_off failed: %d", ret);
	}

	wait_for_console_key();

	k_thread_create(&worker_thread, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			worker_entry, NULL, NULL, NULL, WORKER_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&worker_thread, "bio_station");

	render_splash_state();

	while (1) {
		while (k_msgq_get(&ui_evt_msgq, &evt, K_NO_WAIT) == 0) {
			process_ui_event(&evt);
		}

		step_state_machine();

		k_msleep(MIN(lv_timer_handler(), 10U));
	}
}
