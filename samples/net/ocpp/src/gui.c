/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include "gui.h"

LOG_MODULE_REGISTER(gui, CONFIG_LOG_DEFAULT_LEVEL);

#define NUM_CONNECTORS 2

struct connector_view {
	lv_obj_t *card;
	lv_obj_t *title;
	lv_obj_t *soc_bar;
	lv_obj_t *soc_label;
	lv_obj_t *voltage_container; /* Container for voltage number and unit */
	lv_obj_t *voltage_value;     /* Voltage number */
	lv_obj_t *voltage_unit;      /* Voltage unit "V" */
	lv_obj_t *current_container; /* Container for current number and unit */
	lv_obj_t *current_value;     /* Current number */
	lv_obj_t *current_unit;      /* Current unit "A" */
	bool is_charging;
};

struct gui_state {
	bool network_up;
	char ocpp_status[32];
	struct {
		uint32_t meter_wh;
		uint8_t soc_percent;
		int voltage_v;
		int current_a;
		int power_w;
		bool is_charging;
	} connector[NUM_CONNECTORS];
	char notification[64];
};

static struct gui_state state;
static struct connector_view views[NUM_CONNECTORS];
static lv_obj_t *status_badge_net;
static lv_obj_t *status_badge_ocpp;
static lv_obj_t *notif_toast;
static const struct device *display_dev;
static struct k_mutex state_lock;
static struct k_thread ui_thread;
K_KERNEL_STACK_DEFINE(ui_stack, 12000);

static void with_lock(void (*fn)(void *), void *arg)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	fn(arg);
	k_mutex_unlock(&state_lock);
}

static lv_obj_t *make_badge(const char *text, lv_color_t color)
{
	lv_obj_t *badge = lv_button_create(lv_screen_active());
	lv_obj_set_style_bg_color(badge, color, 0);
	lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(badge, 16, 0);
	lv_obj_set_style_pad_all(badge, 12, 0);
	lv_obj_clear_flag(badge, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_t *label = lv_label_create(badge);
	lv_obj_set_style_text_color(label, lv_color_white(), 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
	lv_label_set_text(label, text);
	return badge;
}

static void style_card(lv_obj_t *card)
{
	lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(card, lv_color_make(45, 50, 58), 0);
	lv_obj_set_style_radius(card, 30, 0);
	// lv_obj_set_style_pad_all(card, 20, 0);
	lv_obj_set_style_shadow_width(card, 20, 0);
	lv_obj_set_style_shadow_color(card, lv_color_make(0, 0, 0), 0);
	lv_obj_set_style_shadow_opa(card, LV_OPA_50, 0);
	lv_obj_set_style_border_width(card, 4, 0);
	lv_obj_set_style_border_color(card, lv_color_make(140, 140, 140), 0);
}

static void build_connector_card(int idx)
{
	struct connector_view *v = &views[idx];
	v->card = lv_obj_create(lv_screen_active());
	style_card(v->card);

	char title[32];
	snprintk(title, sizeof(title), "Connector %d", idx + 1);
	v->title = lv_label_create(v->card);
	lv_obj_set_style_text_color(v->title, lv_color_white(), 0);
	lv_obj_set_style_text_font(v->title, &lv_font_montserrat_18, 0);
	lv_label_set_text(v->title, title);
	lv_obj_set_style_pad_bottom(v->title, 16, 0);

	v->soc_label = lv_label_create(v->card);
	lv_obj_set_style_text_color(v->soc_label, lv_color_white(), 0);
	lv_obj_set_style_text_font(v->soc_label, &lv_font_montserrat_12, 0);
	lv_label_set_text(v->soc_label, "SOC 0%");
	lv_obj_set_style_pad_bottom(v->soc_label, 8, 0);

	v->soc_bar = lv_bar_create(v->card);
	lv_obj_set_size(v->soc_bar, 320, 24);
	lv_bar_set_range(v->soc_bar, 0, 100);
	lv_bar_set_value(v->soc_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(v->soc_bar, lv_color_make(30, 35, 40), 0);
	lv_obj_set_style_bg_opa(v->soc_bar, LV_OPA_COVER, 0);
	lv_obj_set_style_radius(v->soc_bar, 12, 0);
	lv_obj_set_style_pad_all(v->soc_bar, 3, 0);
	lv_obj_set_style_bg_color(v->soc_bar, lv_color_make(60, 180, 100), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(v->soc_bar, LV_OPA_COVER, LV_PART_INDICATOR);
	lv_obj_set_style_radius(v->soc_bar, 10, LV_PART_INDICATOR);
	lv_obj_set_style_pad_all(v->soc_bar, 3, LV_PART_INDICATOR);
	lv_obj_set_style_border_width(v->soc_bar, 0, LV_PART_INDICATOR);
	lv_obj_set_style_outline_width(v->soc_bar, 0, LV_PART_INDICATOR);
	lv_obj_set_style_pad_bottom(v->soc_bar, 16, 0);

	/* Voltage display with large font (planning for 5 digits) */
	v->voltage_container = lv_obj_create(v->card);
	lv_obj_remove_style(v->voltage_container, NULL, LV_PART_MAIN);
	lv_obj_set_style_pad_all(v->voltage_container, 0, 0);
	lv_obj_set_style_bg_opa(v->voltage_container, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(v->voltage_container, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(v->voltage_container, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(v->voltage_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	v->voltage_value = lv_label_create(v->voltage_container);
	lv_obj_set_style_text_color(v->voltage_value, lv_color_make(200, 200, 200), 0);
	lv_obj_set_style_text_font(v->voltage_value, &lv_font_montserrat_28, 0);
	lv_label_set_text(v->voltage_value, "000");
	lv_obj_set_width(v->voltage_value, 80);
	v->voltage_unit = lv_label_create(v->voltage_container);
	lv_obj_set_style_text_color(v->voltage_unit, lv_color_make(200, 200, 200), 0);
	lv_obj_set_style_text_font(v->voltage_unit, &lv_font_montserrat_28,
				   0); /* Larger font for bold effect */
	lv_label_set_text(v->voltage_unit, " V");
	lv_obj_set_style_pad_bottom(v->voltage_container, 8, 0);

	/* Current display with large font (planning for 5 digits) */
	v->current_container = lv_obj_create(v->card);
	lv_obj_remove_style(v->current_container, NULL, LV_PART_MAIN);
	lv_obj_set_style_pad_all(v->current_container, 0, 0);
	lv_obj_set_style_bg_opa(v->current_container, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_opa(v->current_container, LV_OPA_TRANSP, 0);
	lv_obj_set_flex_flow(v->current_container, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(v->current_container, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	v->current_value = lv_label_create(v->current_container);
	lv_obj_set_style_text_color(v->current_value, lv_color_make(200, 200, 200), 0);
	lv_obj_set_style_text_font(v->current_value, &lv_font_montserrat_28, 0);
	lv_label_set_text(v->current_value, "000");
	lv_obj_set_width(v->current_value, 80);
	v->current_unit = lv_label_create(v->current_container);
	lv_obj_set_style_text_color(v->current_unit, lv_color_make(200, 200, 200), 0);
	lv_obj_set_style_text_font(v->current_unit, &lv_font_montserrat_28,
				   0); /* Larger font for bold effect */
	lv_label_set_text(v->current_unit, " A");

	/* Layout inside card */
	lv_obj_set_flex_flow(v->card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(v->card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_size(v->card, 360, 360);
}

static void layout_screen(void)
{
	lv_obj_t *root = lv_screen_active();

	/* Gradient background with better contrast */
	static lv_style_t bg_style;
	lv_style_init(&bg_style);
	lv_style_set_bg_opa(&bg_style, LV_OPA_COVER);
	lv_style_set_bg_grad_dir(&bg_style, LV_GRAD_DIR_VER);
	lv_style_set_bg_color(&bg_style, lv_color_make(15, 18, 22));
	lv_style_set_bg_grad_color(&bg_style, lv_color_make(8, 10, 12));
	lv_obj_add_style(root, &bg_style, 0);

	/* Header title */
	lv_obj_t *title = lv_label_create(root);
	lv_obj_set_style_text_color(title, lv_color_white(), 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_label_set_text(title, "EV Charging Station");
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 20);

	/* Status badges */
	status_badge_net = make_badge("NET: offline", lv_color_make(200, 50, 50));
	lv_obj_align(status_badge_net, LV_ALIGN_TOP_RIGHT, -20, 20);

	status_badge_ocpp = make_badge("OCPP: init", lv_color_make(200, 120, 50));
	lv_obj_align(status_badge_ocpp, LV_ALIGN_TOP_RIGHT, -150, 20);

	/* Connector cards side-by-side with better spacing */
	build_connector_card(0);
	build_connector_card(1);
	lv_obj_align(views[0].card, LV_ALIGN_LEFT_MID, 20, 20);
	lv_obj_align(views[1].card, LV_ALIGN_RIGHT_MID, -20, 20);

	/* Notification toast with better visibility */
	notif_toast = lv_label_create(root);
	lv_obj_set_style_text_color(notif_toast, lv_color_make(255, 180, 60), 0);
	lv_obj_set_style_text_font(notif_toast, &lv_font_montserrat_12, 0);
	lv_label_set_text(notif_toast, "");
	lv_obj_align(notif_toast, LV_ALIGN_BOTTOM_MID, 0, -16);
}

static void update_badges_locked(void *unused)
{
	ARG_UNUSED(unused);
	const char *net_txt = state.network_up ? "NET: online" : "NET: offline";
	lv_obj_set_style_bg_color(
		status_badge_net,
		state.network_up ? lv_color_make(50, 200, 120) : lv_color_make(200, 50, 50), 0);
	lv_label_set_text(lv_obj_get_child(status_badge_net, 0), net_txt);

	lv_label_set_text(lv_obj_get_child(status_badge_ocpp, 0), state.ocpp_status);
}

static void update_connectors_locked(void *unused)
{
	ARG_UNUSED(unused);
	for (int i = 0; i < NUM_CONNECTORS; i++) {
		struct connector_view *v = &views[i];
		const uint8_t soc = state.connector[i].soc_percent;
		char buf[32];
		lv_bar_set_value(v->soc_bar, soc, LV_ANIM_ON);
		snprintk(buf, sizeof(buf), "SOC %u%%", soc);
		lv_label_set_text(v->soc_label, buf);

		/* Update voltage display */
		snprintk(buf, sizeof(buf), "%03d", state.connector[i].voltage_v);
		lv_label_set_text(v->voltage_value, buf);

		/* Update current display */
		snprintk(buf, sizeof(buf), "%03d", state.connector[i].current_a);
		lv_label_set_text(v->current_value, buf);

		/* Enhanced visual cue if charging with better contrast */
		lv_obj_set_style_bg_color(v->card,
					  state.connector[i].is_charging
						  ? lv_color_make(50, 80, 60)
						  : lv_color_make(45, 50, 58),
					  0);
		lv_obj_set_style_border_color(v->card,
					      state.connector[i].is_charging
						      ? lv_color_make(60, 180, 100)
						      : lv_color_make(70, 75, 85),
					      0);
	}
}

static void update_notification_locked(void *unused)
{
	ARG_UNUSED(unused);
	lv_label_set_text(notif_toast, state.notification);
}

static void ui_thread_entry(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	layout_screen();
	/* Perform a couple of refresh ticks before unblank to avoid top-line artifacts */
	for (int i = 0; i < 3; i++) {
		lv_timer_handler();
		k_msleep(10);
	}
	display_blanking_off(display_dev);

	/* First refresh must happen immediately, before any network/OCPP work */
	with_lock(update_badges_locked, NULL);
	with_lock(update_connectors_locked, NULL);
	with_lock(update_notification_locked, NULL);

	while (1) {
		/* Periodic GUI maintenance */
		lv_timer_handler();
		/* Refresh our widgets roughly every 100 ms */
		with_lock(update_badges_locked, NULL);
		with_lock(update_connectors_locked, NULL);
		with_lock(update_notification_locked, NULL);
		k_sleep(K_MSEC(100));
	}
}

int gui_init(void)
{
	k_mutex_init(&state_lock);
	/* Defaults */
	k_mutex_lock(&state_lock, K_FOREVER);
	state.network_up = false;
	snprintk(state.ocpp_status, sizeof(state.ocpp_status), "OCPP: init");
	for (int i = 0; i < NUM_CONNECTORS; i++) {
		state.connector[i].meter_wh = 0;
		state.connector[i].soc_percent = 0;
		state.connector[i].voltage_v = 0;
		state.connector[i].current_a = 0;
		state.connector[i].power_w = 0;
		state.connector[i].is_charging = false;
	}
	state.notification[0] = '\0';
	k_mutex_unlock(&state_lock);

	/* Launch UI thread immediately */
	k_thread_create(&ui_thread, ui_stack, K_KERNEL_STACK_SIZEOF(ui_stack), ui_thread_entry,
			NULL, NULL, NULL, K_PRIO_PREEMPT(7), 0, K_NO_WAIT);
	k_thread_name_set(&ui_thread, "ui");
	return 0;
}

void gui_set_network_status(bool is_up)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	state.network_up = is_up;
	k_mutex_unlock(&state_lock);
}

void gui_set_ocpp_status(const char *status_text)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	snprintk(state.ocpp_status, sizeof(state.ocpp_status), "%s",
		 status_text ? status_text : "");
	k_mutex_unlock(&state_lock);
}

void gui_update_connector(int id, uint32_t meter_wh, uint8_t soc_percent, int voltage_v,
			  int current_a, int power_w, bool is_charging)
{
	if (id < 1 || id > NUM_CONNECTORS) {
		return;
	}
	const int idx = id - 1;
	k_mutex_lock(&state_lock, K_FOREVER);
	state.connector[idx].meter_wh = meter_wh;
	state.connector[idx].soc_percent = soc_percent;
	state.connector[idx].voltage_v = voltage_v;
	state.connector[idx].current_a = current_a;
	state.connector[idx].power_w = power_w;
	state.connector[idx].is_charging = is_charging;
	k_mutex_unlock(&state_lock);
}

void gui_show_notification(const char *text)
{
	k_mutex_lock(&state_lock, K_FOREVER);
	snprintk(state.notification, sizeof(state.notification), "%s", text ? text : "");
	k_mutex_unlock(&state_lock);
}
