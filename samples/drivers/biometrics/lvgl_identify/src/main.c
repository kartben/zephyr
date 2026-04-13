/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <lvgl.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

#define BIOMETRICS_NODE DT_ALIAS(biometrics)

#if !DT_NODE_EXISTS(BIOMETRICS_NODE)
#error "Devicetree alias 'biometrics' must point at a biometric device (see README.rst)"
#endif

enum {
	BIO_WORKER_CMD_IDENTIFY = 1,
};

enum {
	BIO_UI_EVT_IDENTIFY_COMPLETE = 1,
};

struct bio_worker_cmd {
	uint32_t cmd;
};

struct bio_ui_evt {
	uint32_t type;
	int32_t result;
	struct biometric_match_result match;
};

K_MSGQ_DEFINE(cmd_msgq, sizeof(struct bio_worker_cmd), 4, 4);
K_MSGQ_DEFINE(evt_msgq, sizeof(struct bio_ui_evt), 8, 4);

static const struct device *bio_dev = DEVICE_DT_GET(BIOMETRICS_NODE);
static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));

K_THREAD_STACK_DEFINE(worker_stack, 4096);
static struct k_thread worker_thread_data;

static lv_obj_t *scr_idle;
static lv_obj_t *scr_noidentities;
static lv_obj_t *scr_busy;
static lv_obj_t *scr_result;

static lv_obj_t *idle_subtitle;
static lv_obj_t *busy_spinner;
static lv_obj_t *busy_hint;
static lv_obj_t *result_title;
static lv_obj_t *result_detail;

static lv_timer_t *flow_timer;

static bool have_templates;

static void cancel_flow_timer(void)
{
	if (flow_timer != NULL) {
		lv_timer_delete(flow_timer);
		flow_timer = NULL;
	}
}

static void post_worker_cmd(uint32_t cmd)
{
	const struct bio_worker_cmd c = {.cmd = cmd};

	if (k_msgq_put(&cmd_msgq, &c, K_NO_WAIT) != 0) {
		LOG_ERR("Command queue full");
	}
}

static void send_ui_evt(const struct bio_ui_evt *evt)
{
	if (k_msgq_put(&evt_msgq, evt, K_MSEC(500)) != 0) {
		LOG_ERR("UI event queue full");
	}
}

static void style_screen(lv_obj_t *scr)
{
	lv_obj_set_style_bg_color(scr, lv_color_hex(0x1a1a2e), 0);
	lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
	lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
}

static void style_card(lv_obj_t *obj)
{
	lv_obj_set_style_bg_color(obj, lv_color_hex(0x16213e), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(obj, 16, 0);
	lv_obj_set_style_pad_all(obj, 28, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
}

static void start_identify_round(lv_timer_t *t)
{
	ARG_UNUSED(t);

	flow_timer = NULL;
	lv_label_set_text(busy_hint, "Authenticating...");
	lv_scr_load(scr_busy);
	lv_obj_clear_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);
	post_worker_cmd(BIO_WORKER_CMD_IDENTIFY);
}

static void dismiss_result(lv_timer_t *t)
{
	ARG_UNUSED(t);

	flow_timer = NULL;
	if (!have_templates) {
		return;
	}
	lv_scr_load(scr_idle);
	flow_timer = lv_timer_create(start_identify_round, 1800, NULL);
	lv_timer_set_repeat_count(flow_timer, 1);
}

static void idle_dots_anim(lv_timer_t *t)
{
	static const char *const dots[] = {"", ".", "..", "..."};
	static int i;
	lv_obj_t *lbl = lv_timer_get_user_data(t);

	if (lv_screen_active() != scr_idle) {
		return;
	}

	i = (i + 1) % 4;
	lv_label_set_text_fmt(lbl, "Next check%s", dots[i]);
}

static void build_ui(void)
{
	lv_obj_t *card;
	lv_obj_t *lbl;

	/* --- Idle (between automatic identification rounds) --- */
	scr_idle = lv_obj_create(NULL);
	style_screen(scr_idle);
	lv_obj_set_size(scr_idle, LV_PCT(100), LV_PCT(100));

	card = lv_obj_create(scr_idle);
	style_card(card);
	lv_obj_set_size(card, 720, 320);
	lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl, "Identity check");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xe94560), 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl, "Fully automatic - no buttons or touch.");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xb8b8d1), 0);
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(lbl, 660);
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 88);

	idle_subtitle = lv_label_create(card);
	lv_label_set_text(idle_subtitle, "Next check");
	lv_obj_set_style_text_font(idle_subtitle, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(idle_subtitle, lv_color_hex(0x8888aa), 0);
	lv_obj_align(idle_subtitle, LV_ALIGN_BOTTOM_MID, 0, -24);

	/* --- No templates --- */
	scr_noidentities = lv_obj_create(NULL);
	style_screen(scr_noidentities);
	lv_obj_set_size(scr_noidentities, LV_PCT(100), LV_PCT(100));

	card = lv_obj_create(scr_noidentities);
	style_card(card);
	lv_obj_set_size(card, 720, 340);
	lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl, "No enrolled identities");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xe94560), 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 24);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl,
			  "This demo only identifies users. The sensor has no templates yet.\n\n"
			  "Provision identities on the module (or use another build), then run "
			  "again.");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xb8b8d1), 0);
	lv_label_set_long_mode(lbl, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(lbl, 660);
	lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 96);

	/* --- Busy --- */
	scr_busy = lv_obj_create(NULL);
	style_screen(scr_busy);
	lv_obj_set_size(scr_busy, LV_PCT(100), LV_PCT(100));

	card = lv_obj_create(scr_busy);
	style_card(card);
	lv_obj_set_size(card, 520, 280);
	lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl, "Identifying");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0xeeeeee), 0);
	lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 28);

	busy_spinner = lv_spinner_create(card);
	lv_obj_set_size(busy_spinner, 80, 80);
	lv_obj_align(busy_spinner, LV_ALIGN_CENTER, 0, -10);

	busy_hint = lv_label_create(card);
	lv_label_set_text(busy_hint, "Hold still...");
	lv_obj_set_style_text_font(busy_hint, &lv_font_montserrat_18, 0);
	lv_obj_set_style_text_color(busy_hint, lv_color_hex(0xb8b8d1), 0);
	lv_obj_align(busy_hint, LV_ALIGN_BOTTOM_MID, 0, -24);

	/* --- Result --- */
	scr_result = lv_obj_create(NULL);
	style_screen(scr_result);
	lv_obj_set_size(scr_result, LV_PCT(100), LV_PCT(100));

	card = lv_obj_create(scr_result);
	style_card(card);
	lv_obj_set_size(card, 720, 360);
	lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);

	result_title = lv_label_create(card);
	lv_label_set_text(result_title, "Result");
	lv_obj_set_style_text_font(result_title, &lv_font_montserrat_32, 0);
	lv_obj_set_style_text_align(result_title, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_width(result_title, 660);
	lv_obj_align(result_title, LV_ALIGN_TOP_MID, 0, 28);

	result_detail = lv_label_create(card);
	lv_label_set_text(result_detail, "-");
	lv_obj_set_style_text_font(result_detail, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_align(result_detail, LV_TEXT_ALIGN_CENTER, 0);
	lv_label_set_long_mode(result_detail, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(result_detail, 660);
	lv_obj_align(result_detail, LV_ALIGN_TOP_MID, 0, 100);

	lbl = lv_label_create(card);
	lv_label_set_text(lbl, "Continuing automatically...");
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0x8888aa), 0);
	lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -20);
}

static bool templates_empty(void)
{
	uint16_t ids[4];
	size_t count = 0;
	int ret;

	ret = biometric_template_list(bio_dev, ids, ARRAY_SIZE(ids), &count);
	if (ret < 0) {
		LOG_ERR("template_list failed: %d", ret);
		return true;
	}

	return count == 0U;
}

static void worker_identify(void)
{
	int ret;
	struct bio_ui_evt evt = {0};
	struct biometric_match_result match = {0};

	ret = biometric_match(bio_dev, BIOMETRIC_MATCH_IDENTIFY, 0, K_SECONDS(20), &match);
	evt.type = BIO_UI_EVT_IDENTIFY_COMPLETE;
	evt.result = ret;
	evt.match = match;
	send_ui_evt(&evt);
}

static void worker_entry(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		struct bio_worker_cmd cmd;

		k_msgq_get(&cmd_msgq, &cmd, K_FOREVER);

		if (cmd.cmd == BIO_WORKER_CMD_IDENTIFY) {
			worker_identify();
		}
	}
}

static void handle_ui_evt(const struct bio_ui_evt *evt)
{
	char buf[160];

	if (evt->type != BIO_UI_EVT_IDENTIFY_COMPLETE) {
		return;
	}

	lv_obj_add_flag(busy_spinner, LV_OBJ_FLAG_HIDDEN);

	if (evt->result == 0) {
		lv_label_set_text(result_title, "OK");
		lv_obj_set_style_text_color(result_title, lv_color_hex(0x4ecca3), 0);
		snprintf(buf, sizeof(buf), "Welcome back.\nUser ID %u - confidence %d, quality %u",
			 evt->match.template_id, evt->match.confidence, evt->match.image_quality);
		lv_label_set_text(result_detail, buf);
	} else if (evt->result == -ENOENT) {
		lv_label_set_text(result_title, "Not recognized");
		lv_obj_set_style_text_color(result_title, lv_color_hex(0xe94560), 0);
		lv_label_set_text(result_detail, "No matching user. Next round starts shortly.");
	} else if (evt->result == -ETIMEDOUT) {
		lv_label_set_text(result_title, "Timed out");
		lv_obj_set_style_text_color(result_title, lv_color_hex(0xf0a500), 0);
		lv_label_set_text(result_detail, "Sensor did not finish in time. Retrying soon.");
	} else {
		snprintf(buf, sizeof(buf), "Identification error (%d)", evt->result);
		lv_label_set_text(result_title, "Error");
		lv_obj_set_style_text_color(result_title, lv_color_hex(0xe94560), 0);
		lv_label_set_text(result_detail, buf);
	}

	lv_scr_load(scr_result);
	cancel_flow_timer();
	flow_timer = lv_timer_create(dismiss_result, 4200, NULL);
	lv_timer_set_repeat_count(flow_timer, 1);
}

int main(void)
{
	int ret;
	struct bio_ui_evt evt;

	if (!device_is_ready(bio_dev)) {
		LOG_ERR("Biometrics device not ready");
		return 0;
	}

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display not ready");
		return 0;
	}

	k_thread_create(&worker_thread_data, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			worker_entry, NULL, NULL, NULL, 10, 0, K_NO_WAIT);
	k_thread_name_set(&worker_thread_data, "bio_worker");

	build_ui();

	ret = display_blanking_off(display_dev);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("display_blanking_off: %d", ret);
	}

	have_templates = !templates_empty();

	if (!have_templates) {
		lv_scr_load(scr_noidentities);
	} else {
		lv_scr_load(scr_idle);
		lv_timer_t *dots = lv_timer_create(idle_dots_anim, 700, idle_subtitle);

		lv_timer_set_repeat_count(dots, -1);
		flow_timer = lv_timer_create(start_identify_round, 1200, NULL);
		lv_timer_set_repeat_count(flow_timer, 1);
	}

	while (1) {
		while (k_msgq_get(&evt_msgq, &evt, K_NO_WAIT) == 0) {
			handle_ui_evt(&evt);
		}

		uint32_t delay_ms = lv_timer_handler();

		k_msleep(MIN(delay_ms, 10U));
	}
}
