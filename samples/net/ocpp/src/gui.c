/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include "gui.h"

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gui);

/* GUI objects */
static lv_obj_t *status_label;
static lv_obj_t *ip_label;
static lv_obj_t *connector_labels[2];
static lv_obj_t *idtag_labels[2];
static lv_obj_t *meter_labels[2];
static lv_obj_t *state_labels[2];
static lv_obj_t *connector_arcs[2];

/* GUI state */
static bool gui_initialized = false;

/* Color definitions for nice UI */
#define COLOR_PRIMARY   lv_color_hex(0x2196F3)
#define COLOR_SUCCESS   lv_color_hex(0x4CAF50)
#define COLOR_WARNING   lv_color_hex(0xFF9800)
#define COLOR_ERROR     lv_color_hex(0xF44336)
#define COLOR_BACKGROUND lv_color_hex(0x263238)
#define COLOR_CARD      lv_color_hex(0x37474F)

static lv_style_t style_card;
static lv_style_t style_title;
static lv_style_t style_label;
static lv_style_t style_value;

static void create_styles(void)
{
	/* Card style */
	lv_style_init(&style_card);
	lv_style_set_bg_color(&style_card, COLOR_CARD);
	lv_style_set_radius(&style_card, 10);
	lv_style_set_pad_all(&style_card, 10);
	lv_style_set_border_width(&style_card, 2);
	lv_style_set_border_color(&style_card, COLOR_PRIMARY);

	/* Title style */
	lv_style_init(&style_title);
	lv_style_set_text_color(&style_title, lv_color_white());
	lv_style_set_text_font(&style_title, &lv_font_montserrat_18);

	/* Label style */
	lv_style_init(&style_label);
	lv_style_set_text_color(&style_label, lv_palette_main(LV_PALETTE_BLUE_GREY));
	lv_style_set_text_font(&style_label, &lv_font_montserrat_14);

	/* Value style */
	lv_style_init(&style_value);
	lv_style_set_text_color(&style_value, lv_color_white());
	lv_style_set_text_font(&style_value, &lv_font_montserrat_16);
}

static lv_obj_t *create_connector_card(lv_obj_t *parent, int connector_id, int x, int y)
{
	lv_obj_t *card;
	lv_obj_t *title;
	lv_obj_t *arc;
	char title_text[32];

	/* Create card */
	card = lv_obj_create(parent);
	lv_obj_add_style(card, &style_card, 0);
	lv_obj_set_size(card, 220, 200);
	lv_obj_set_pos(card, x, y);

	/* Title */
	snprintf(title_text, sizeof(title_text), "Connector %d", connector_id);
	title = lv_label_create(card);
	lv_label_set_text(title, title_text);
	lv_obj_add_style(title, &style_title, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

	/* Arc for visual status */
	arc = lv_arc_create(card);
	lv_obj_set_size(arc, 80, 80);
	lv_obj_align(arc, LV_ALIGN_CENTER, 0, -15);
	lv_arc_set_value(arc, 0);
	lv_arc_set_bg_angles(arc, 0, 360);
	lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
	connector_arcs[connector_id - 1] = arc;

	/* State label */
	state_labels[connector_id - 1] = lv_label_create(card);
	lv_label_set_text(state_labels[connector_id - 1], "Idle");
	lv_obj_add_style(state_labels[connector_id - 1], &style_value, 0);
	lv_obj_align(state_labels[connector_id - 1], LV_ALIGN_CENTER, 0, 40);

	/* ID Tag label */
	idtag_labels[connector_id - 1] = lv_label_create(card);
	lv_label_set_text(idtag_labels[connector_id - 1], "ID: ---");
	lv_obj_add_style(idtag_labels[connector_id - 1], &style_label, 0);
	lv_obj_align(idtag_labels[connector_id - 1], LV_ALIGN_BOTTOM_LEFT, 5, -25);

	/* Meter value label */
	meter_labels[connector_id - 1] = lv_label_create(card);
	lv_label_set_text(meter_labels[connector_id - 1], "0 Wh");
	lv_obj_add_style(meter_labels[connector_id - 1], &style_label, 0);
	lv_obj_align(meter_labels[connector_id - 1], LV_ALIGN_BOTTOM_LEFT, 5, -5);

	return card;
}

int gui_init(void)
{
	const struct device *display_dev;
	lv_obj_t *screen;
	lv_obj_t *header_card;
	lv_obj_t *title;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -ENODEV;
	}

	LOG_INF("Initializing LVGL GUI");

	/* Get active screen */
	screen = lv_screen_active();
	lv_obj_set_style_bg_color(screen, COLOR_BACKGROUND, 0);

	/* Create styles */
	create_styles();

	/* Header card */
	header_card = lv_obj_create(screen);
	lv_obj_add_style(header_card, &style_card, 0);
	lv_obj_set_size(header_card, 460, 60);
	lv_obj_set_pos(header_card, 10, 10);

	/* Title */
	title = lv_label_create(header_card);
	lv_label_set_text(title, "OCPP Charge Point");
	lv_obj_add_style(title, &style_title, 0);
	lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 5);

	/* Status label */
	status_label = lv_label_create(header_card);
	lv_label_set_text(status_label, "Status: Initializing...");
	lv_obj_add_style(status_label, &style_label, 0);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_LEFT, 10, -5);

	/* IP address label */
	ip_label = lv_label_create(header_card);
	lv_label_set_text(ip_label, "IP: ---");
	lv_obj_add_style(ip_label, &style_label, 0);
	lv_obj_align(ip_label, LV_ALIGN_BOTTOM_RIGHT, -10, -5);

	/* Create connector cards */
	create_connector_card(screen, 1, 10, 80);
	create_connector_card(screen, 2, 240, 80);

	lv_timer_handler();
	display_blanking_off(display_dev);

	gui_initialized = true;
	LOG_INF("LVGL GUI initialized successfully");

	return 0;
}

void gui_update_status(const char *status)
{
	if (!gui_initialized || !status_label) {
		return;
	}

	char text[64];
	snprintf(text, sizeof(text), "Status: %s", status);
	lv_label_set_text(status_label, text);
	lv_timer_handler();
}

void gui_update_ip(const char *ip)
{
	if (!gui_initialized || !ip_label) {
		return;
	}

	char text[64];
	snprintf(text, sizeof(text), "IP: %s", ip);
	lv_label_set_text(ip_label, text);
	lv_timer_handler();
}

void gui_update_connector_state(int connector_id, enum gui_connector_state state)
{
	if (!gui_initialized || connector_id < 1 || connector_id > 2) {
		return;
	}

	int idx = connector_id - 1;
	const char *state_text;
	lv_color_t arc_color;
	int arc_value;

	switch (state) {
	case GUI_CONNECTOR_IDLE:
		state_text = "Idle";
		arc_color = lv_palette_main(LV_PALETTE_GREY);
		arc_value = 0;
		break;
	case GUI_CONNECTOR_AUTHORIZING:
		state_text = "Authorizing";
		arc_color = COLOR_WARNING;
		arc_value = 33;
		break;
	case GUI_CONNECTOR_CHARGING:
		state_text = "Charging";
		arc_color = COLOR_SUCCESS;
		arc_value = 100;
		break;
	case GUI_CONNECTOR_ERROR:
		state_text = "Error";
		arc_color = COLOR_ERROR;
		arc_value = 0;
		break;
	default:
		state_text = "Unknown";
		arc_color = lv_palette_main(LV_PALETTE_GREY);
		arc_value = 0;
		break;
	}

	lv_label_set_text(state_labels[idx], state_text);
	lv_arc_set_value(connector_arcs[idx], arc_value);
	lv_obj_set_style_arc_color(connector_arcs[idx], arc_color, LV_PART_INDICATOR);
	lv_timer_handler();
}

void gui_update_connector_idtag(int connector_id, const char *idtag)
{
	if (!gui_initialized || connector_id < 1 || connector_id > 2 || !idtag) {
		return;
	}

	int idx = connector_id - 1;
	char text[64];
	snprintf(text, sizeof(text), "ID: %s", idtag);
	lv_label_set_text(idtag_labels[idx], text);
	lv_timer_handler();
}

void gui_update_connector_meter(int connector_id, int watt_hours)
{
	if (!gui_initialized || connector_id < 1 || connector_id > 2) {
		return;
	}

	int idx = connector_id - 1;
	char text[64];
	snprintf(text, sizeof(text), "%d Wh", watt_hours);
	lv_label_set_text(meter_labels[idx], text);
	lv_timer_handler();
}

void gui_task(void)
{
	while (1) {
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}
}
