/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ocpp_gui, LOG_LEVEL_INF);

/* Color scheme - Modern EV Charging Station theme */
#define COLOR_PRIMARY     0x00D9FF  /* Bright cyan for charging */
#define COLOR_SECONDARY   0x1E3A8A  /* Deep blue for background */
#define COLOR_SUCCESS     0x10B981  /* Green for active/success */
#define COLOR_WARNING     0xF59E0B  /* Amber for warnings */
#define COLOR_ERROR       0xEF4444  /* Red for errors */
#define COLOR_BG_DARK     0x0F172A  /* Very dark blue background */
#define COLOR_BG_CARD     0x1E293B  /* Card background */
#define COLOR_TEXT_WHITE  0xFFFFFF  /* White text */

/* GUI elements */
static lv_obj_t *screen;
static lv_obj_t *status_label;
static lv_obj_t *conn1_card;
static lv_obj_t *conn2_card;
static lv_obj_t *conn1_status_label;
static lv_obj_t *conn2_status_label;
static lv_obj_t *conn1_power_label;
static lv_obj_t *conn2_power_label;
static lv_obj_t *conn1_energy_label;
static lv_obj_t *conn2_energy_label;
static lv_obj_t *conn1_indicator;
static lv_obj_t *conn2_indicator;
static lv_obj_t *network_status_label;
static lv_obj_t *time_label;

/* State tracking */
static bool gui_initialized = false;
static uint32_t conn1_energy = 0;
static uint32_t conn2_energy = 0;

static lv_obj_t *create_connector_card(lv_obj_t *parent, const char *title, 
                                        int x_offset, int y_offset)
{
	lv_obj_t *card = lv_obj_create(parent);
	lv_obj_set_size(card, 180, 140);
	lv_obj_set_pos(card, x_offset, y_offset);
	lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_BG_CARD), 0);
	lv_obj_set_style_border_color(card, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_set_style_border_width(card, 2, 0);
	lv_obj_set_style_radius(card, 10, 0);
	lv_obj_set_style_pad_all(card, 10, 0);

	/* Card title */
	lv_obj_t *title_label = lv_label_create(card);
	lv_label_set_text(title_label, title);
	lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_14, 0);
	lv_obj_align(title_label, LV_ALIGN_TOP_MID, 0, 0);

	return card;
}

static lv_obj_t *create_status_indicator(lv_obj_t *parent)
{
	lv_obj_t *indicator = lv_obj_create(parent);
	lv_obj_set_size(indicator, 20, 20);
	lv_obj_set_style_radius(indicator, LV_RADIUS_CIRCLE, 0);
	lv_obj_set_style_bg_color(indicator, lv_color_hex(0x6B7280), 0);
	lv_obj_set_style_border_width(indicator, 0, 0);
	lv_obj_align(indicator, LV_ALIGN_TOP_RIGHT, -5, 5);
	
	return indicator;
}

void ocpp_gui_init(void)
{
	const struct device *display_dev;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	/* Create main screen */
	screen = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG_DARK), 0);
	lv_screen_load(screen);

	/* Header with title */
	lv_obj_t *header = lv_obj_create(screen);
	lv_obj_set_size(header, 460, 50);
	lv_obj_set_pos(header, 10, 5);
	lv_obj_set_style_bg_color(header, lv_color_hex(COLOR_SECONDARY), 0);
	lv_obj_set_style_border_width(header, 0, 0);
	lv_obj_set_style_radius(header, 8, 0);

	lv_obj_t *title = lv_label_create(header);
	lv_label_set_text(title, "EV CHARGING STATION");
	lv_obj_set_style_text_color(title, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, 0);

	/* System status */
	status_label = lv_label_create(screen);
	lv_label_set_text(status_label, "System: Initializing...");
	lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_WARNING), 0);
	lv_obj_set_pos(status_label, 20, 65);

	/* Connector 1 Card */
	conn1_card = create_connector_card(screen, "CONNECTOR 1", 20, 90);
	
	conn1_indicator = create_status_indicator(conn1_card);
	
	conn1_status_label = lv_label_create(conn1_card);
	lv_label_set_text(conn1_status_label, "Status: Idle");
	lv_obj_set_style_text_color(conn1_status_label, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_set_pos(conn1_status_label, 5, 25);

	conn1_power_label = lv_label_create(conn1_card);
	lv_label_set_text(conn1_power_label, "Power: 0 kW");
	lv_obj_set_style_text_color(conn1_power_label, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_set_style_text_font(conn1_power_label, &lv_font_montserrat_14, 0);
	lv_obj_set_pos(conn1_power_label, 5, 50);

	conn1_energy_label = lv_label_create(conn1_card);
	lv_label_set_text(conn1_energy_label, "Energy: 0 Wh");
	lv_obj_set_style_text_color(conn1_energy_label, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_set_pos(conn1_energy_label, 5, 75);

	lv_obj_t *conn1_id_label = lv_label_create(conn1_card);
	lv_label_set_text(conn1_id_label, "ID: --");
	lv_obj_set_style_text_color(conn1_id_label, lv_color_hex(0x9CA3AF), 0);
	lv_obj_set_style_text_font(conn1_id_label, &lv_font_montserrat_12, 0);
	lv_obj_set_pos(conn1_id_label, 5, 100);

	/* Connector 2 Card */
	conn2_card = create_connector_card(screen, "CONNECTOR 2", 220, 90);
	
	conn2_indicator = create_status_indicator(conn2_card);
	
	conn2_status_label = lv_label_create(conn2_card);
	lv_label_set_text(conn2_status_label, "Status: Idle");
	lv_obj_set_style_text_color(conn2_status_label, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_set_pos(conn2_status_label, 5, 25);

	conn2_power_label = lv_label_create(conn2_card);
	lv_label_set_text(conn2_power_label, "Power: 0 kW");
	lv_obj_set_style_text_color(conn2_power_label, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_set_style_text_font(conn2_power_label, &lv_font_montserrat_14, 0);
	lv_obj_set_pos(conn2_power_label, 5, 50);

	conn2_energy_label = lv_label_create(conn2_card);
	lv_label_set_text(conn2_energy_label, "Energy: 0 Wh");
	lv_obj_set_style_text_color(conn2_energy_label, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_set_pos(conn2_energy_label, 5, 75);

	lv_obj_t *conn2_id_label = lv_label_create(conn2_card);
	lv_label_set_text(conn2_id_label, "ID: --");
	lv_obj_set_style_text_color(conn2_id_label, lv_color_hex(0x9CA3AF), 0);
	lv_obj_set_style_text_font(conn2_id_label, &lv_font_montserrat_12, 0);
	lv_obj_set_pos(conn2_id_label, 5, 100);

	/* Network status panel */
	lv_obj_t *network_panel = lv_obj_create(screen);
	lv_obj_set_size(network_panel, 380, 50);
	lv_obj_set_pos(network_panel, 20, 240);
	lv_obj_set_style_bg_color(network_panel, lv_color_hex(COLOR_BG_CARD), 0);
	lv_obj_set_style_border_color(network_panel, lv_color_hex(0x374151), 0);
	lv_obj_set_style_border_width(network_panel, 1, 0);
	lv_obj_set_style_radius(network_panel, 8, 0);
	lv_obj_set_style_pad_all(network_panel, 8, 0);

	network_status_label = lv_label_create(network_panel);
	lv_label_set_text(network_status_label, "Network: Disconnected");
	lv_obj_set_style_text_color(network_status_label, lv_color_hex(COLOR_WARNING), 0);
	lv_obj_align(network_status_label, LV_ALIGN_LEFT_MID, 0, 0);

	time_label = lv_label_create(network_panel);
	lv_label_set_text(time_label, "00:00");
	lv_obj_set_style_text_color(time_label, lv_color_hex(COLOR_TEXT_WHITE), 0);
	lv_obj_align(time_label, LV_ALIGN_RIGHT_MID, 0, 0);

	/* Footer */
	lv_obj_t *footer = lv_label_create(screen);
	lv_label_set_text(footer, "Zephyr OCPP 1.6");
	lv_obj_set_style_text_color(footer, lv_color_hex(0x6B7280), 0);
	lv_obj_set_style_text_font(footer, &lv_font_montserrat_12, 0);
	lv_obj_align(footer, LV_ALIGN_BOTTOM_MID, 0, -5);

	display_blanking_off(display_dev);
	gui_initialized = true;
	
	LOG_INF("OCPP GUI initialized successfully");
}

void ocpp_gui_update_system_status(const char *status, bool connected)
{
	if (!gui_initialized) {
		return;
	}

	lv_label_set_text_fmt(status_label, "System: %s", status);
	
	if (connected) {
		lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_SUCCESS), 0);
	} else {
		lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_WARNING), 0);
	}
}

void ocpp_gui_update_network_status(const char *ip, bool connected)
{
	if (!gui_initialized) {
		return;
	}

	if (connected && ip) {
		lv_label_set_text_fmt(network_status_label, "Network: %s", ip);
		lv_obj_set_style_text_color(network_status_label, lv_color_hex(COLOR_SUCCESS), 0);
	} else {
		lv_label_set_text(network_status_label, "Network: Disconnected");
		lv_obj_set_style_text_color(network_status_label, lv_color_hex(COLOR_ERROR), 0);
	}
}

void ocpp_gui_update_connector(int connector_id, const char *status, 
                                uint32_t power_w, uint32_t energy_wh,
                                const char *id_tag)
{
	if (!gui_initialized) {
		return;
	}

	lv_obj_t *status_lbl, *power_lbl, *energy_lbl, *indicator;
	bool is_charging = false;

	if (connector_id == 1) {
		status_lbl = conn1_status_label;
		power_lbl = conn1_power_label;
		energy_lbl = conn1_energy_label;
		indicator = conn1_indicator;
		conn1_energy = energy_wh;
	} else if (connector_id == 2) {
		status_lbl = conn2_status_label;
		power_lbl = conn2_power_label;
		energy_lbl = conn2_energy_label;
		indicator = conn2_indicator;
		conn2_energy = energy_wh;
	} else {
		return;
	}

	/* Update status */
	lv_label_set_text_fmt(status_lbl, "Status: %s", status);
	
	/* Determine if charging based on status */
	if (strcmp(status, "Charging") == 0) {
		is_charging = true;
		lv_obj_set_style_text_color(status_lbl, lv_color_hex(COLOR_SUCCESS), 0);
		lv_obj_set_style_bg_color(indicator, lv_color_hex(COLOR_SUCCESS), 0);
	} else if (strcmp(status, "Authorizing") == 0 || strcmp(status, "Preparing") == 0) {
		lv_obj_set_style_text_color(status_lbl, lv_color_hex(COLOR_WARNING), 0);
		lv_obj_set_style_bg_color(indicator, lv_color_hex(COLOR_WARNING), 0);
	} else {
		lv_obj_set_style_text_color(status_lbl, lv_color_hex(COLOR_TEXT_WHITE), 0);
		lv_obj_set_style_bg_color(indicator, lv_color_hex(0x6B7280), 0);
	}

	/* Update power - convert watts to kilowatts for display */
	if (power_w > 0) {
		lv_label_set_text_fmt(power_lbl, "Power: %u.%02u kW", 
		                       power_w / 1000, (power_w % 1000) / 10);
	} else {
		lv_label_set_text(power_lbl, "Power: 0 kW");
	}

	/* Update energy */
	if (energy_wh >= 1000) {
		lv_label_set_text_fmt(energy_lbl, "Energy: %u.%02u kWh", 
		                       energy_wh / 1000, (energy_wh % 1000) / 10);
	} else {
		lv_label_set_text_fmt(energy_lbl, "Energy: %u Wh", energy_wh);
	}
}

void ocpp_gui_update_time(uint32_t timestamp)
{
	if (!gui_initialized) {
		return;
	}

	uint32_t hours = (timestamp / 3600) % 24;
	uint32_t minutes = (timestamp / 60) % 60;
	
	lv_label_set_text_fmt(time_label, "%02u:%02u", hours, minutes);
}

void ocpp_gui_task(void)
{
	if (!gui_initialized) {
		return;
	}

	lv_timer_handler();
}
