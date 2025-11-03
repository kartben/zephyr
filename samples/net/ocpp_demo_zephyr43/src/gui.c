/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>

#include "ocpp_demo.h"

LOG_MODULE_REGISTER(gui, LOG_LEVEL_INF);

/* LVGL objects */
static lv_obj_t *label_title;
static lv_obj_t *label_network;
static lv_obj_t *label_ocpp;
static lv_obj_t *label_usb;
static lv_obj_t *label_uptime;
static lv_obj_t *bar_cpu_load;
static lv_obj_t *label_cpu_load;
static lv_obj_t *label_cpu_freq;

/* Connector panels */
static lv_obj_t *connector_panels[NO_OF_CONN];
static lv_obj_t *connector_labels[NO_OF_CONN];
static lv_obj_t *connector_state_labels[NO_OF_CONN];
static lv_obj_t *connector_meter_labels[NO_OF_CONN];

static const struct device *display_dev;
static bool gui_initialized = false;

/* Color definitions */
#define COLOR_AVAILABLE   lv_palette_main(LV_PALETTE_GREEN)
#define COLOR_PREPARING   lv_palette_main(LV_PALETTE_YELLOW)
#define COLOR_CHARGING    lv_palette_main(LV_PALETTE_BLUE)
#define COLOR_FINISHING   lv_palette_main(LV_PALETTE_ORANGE)
#define COLOR_FAULTED     lv_palette_main(LV_PALETTE_RED)

static const char *get_connector_state_str(enum connector_state state)
{
	switch (state) {
	case CONN_STATE_AVAILABLE:
		return "Available";
	case CONN_STATE_PREPARING:
		return "Preparing";
	case CONN_STATE_CHARGING:
		return "Charging";
	case CONN_STATE_FINISHING:
		return "Finishing";
	case CONN_STATE_FAULTED:
		return "Faulted";
	default:
		return "Unknown";
	}
}

static lv_color_t get_connector_color(enum connector_state state)
{
	switch (state) {
	case CONN_STATE_AVAILABLE:
		return COLOR_AVAILABLE;
	case CONN_STATE_PREPARING:
		return COLOR_PREPARING;
	case CONN_STATE_CHARGING:
		return COLOR_CHARGING;
	case CONN_STATE_FINISHING:
		return COLOR_FINISHING;
	case CONN_STATE_FAULTED:
		return COLOR_FAULTED;
	default:
		return lv_palette_main(LV_PALETTE_GREY);
	}
}

void gui_init(void)
{
	lv_obj_t *screen;
	
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}
	
	LOG_INF("Display device ready: %s", display_dev->name);
	
	/* Create main screen */
	screen = lv_scr_act();
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
	
	/* Title */
	label_title = lv_label_create(screen);
	lv_label_set_text(label_title, "Zephyr 4.3 OCPP Demo");
	lv_obj_set_style_text_color(label_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_title, &lv_font_montserrat_20, LV_PART_MAIN);
	lv_obj_align(label_title, LV_ALIGN_TOP_MID, 0, 5);
	
	/* Status row at top */
	int y_pos = 35;
	
	/* Network status */
	label_network = lv_label_create(screen);
	lv_label_set_text(label_network, "NET: Disconnected");
	lv_obj_set_style_text_color(label_network, lv_color_hex(0xFF0000), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_network, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(label_network, 5, y_pos);
	
	/* OCPP status */
	label_ocpp = lv_label_create(screen);
	lv_label_set_text(label_ocpp, "OCPP: Offline");
	lv_obj_set_style_text_color(label_ocpp, lv_color_hex(0xFF0000), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_ocpp, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(label_ocpp, 150, y_pos);
	
	/* USB status */
	label_usb = lv_label_create(screen);
	lv_label_set_text(label_usb, "USB: No");
	lv_obj_set_style_text_color(label_usb, lv_color_hex(0xFF6600), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_usb, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(label_usb, 300, y_pos);
	
	/* Uptime */
	label_uptime = lv_label_create(screen);
	lv_label_set_text(label_uptime, "Uptime: 0s");
	lv_obj_set_style_text_color(label_uptime, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_uptime, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(label_uptime, 380, y_pos);
	
	y_pos = 55;
	
	/* CPU Load bar */
	lv_obj_t *cpu_panel = lv_obj_create(screen);
	lv_obj_set_size(cpu_panel, 460, 35);
	lv_obj_set_pos(cpu_panel, 10, y_pos);
	lv_obj_set_style_bg_color(cpu_panel, lv_color_hex(0x1a1a1a), LV_PART_MAIN);
	lv_obj_set_style_border_width(cpu_panel, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(cpu_panel, lv_color_hex(0x444444), LV_PART_MAIN);
	
	lv_obj_t *cpu_label_title = lv_label_create(cpu_panel);
	lv_label_set_text(cpu_label_title, "CPU:");
	lv_obj_set_style_text_color(cpu_label_title, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
	lv_obj_set_style_text_font(cpu_label_title, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(cpu_label_title, 5, 8);
	
	bar_cpu_load = lv_bar_create(cpu_panel);
	lv_obj_set_size(bar_cpu_load, 280, 20);
	lv_obj_set_pos(bar_cpu_load, 45, 7);
	lv_bar_set_range(bar_cpu_load, 0, 100);
	lv_bar_set_value(bar_cpu_load, 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(bar_cpu_load, lv_color_hex(0x333333), LV_PART_MAIN);
	lv_obj_set_style_bg_color(bar_cpu_load, lv_color_hex(0x00FF00), LV_PART_INDICATOR);
	
	label_cpu_load = lv_label_create(cpu_panel);
	lv_label_set_text(label_cpu_load, "0%");
	lv_obj_set_style_text_color(label_cpu_load, lv_color_hex(0x00FF00), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_cpu_load, &lv_font_montserrat_14, LV_PART_MAIN);
	lv_obj_set_pos(label_cpu_load, 335, 7);
	
	label_cpu_freq = lv_label_create(cpu_panel);
	lv_label_set_text(label_cpu_freq, "216MHz");
	lv_obj_set_style_text_color(label_cpu_freq, lv_color_hex(0xCCCCCC), LV_PART_MAIN);
	lv_obj_set_style_text_font(label_cpu_freq, &lv_font_montserrat_12, LV_PART_MAIN);
	lv_obj_set_pos(label_cpu_freq, 385, 9);
	
	y_pos = 100;
	
	/* Connector panels */
	for (int i = 0; i < NO_OF_CONN; i++) {
		int x_pos = 10 + (i * 235);
		
		connector_panels[i] = lv_obj_create(screen);
		lv_obj_set_size(connector_panels[i], 225, 160);
		lv_obj_set_pos(connector_panels[i], x_pos, y_pos);
		lv_obj_set_style_bg_color(connector_panels[i], lv_color_hex(0x2a2a2a), LV_PART_MAIN);
		lv_obj_set_style_border_width(connector_panels[i], 2, LV_PART_MAIN);
		lv_obj_set_style_border_color(connector_panels[i], COLOR_AVAILABLE, LV_PART_MAIN);
		
		connector_labels[i] = lv_label_create(connector_panels[i]);
		char buf[32];
		snprintf(buf, sizeof(buf), "Connector %d", i + 1);
		lv_label_set_text(connector_labels[i], buf);
		lv_obj_set_style_text_color(connector_labels[i], lv_color_hex(0xFFFFFF), LV_PART_MAIN);
		lv_obj_set_style_text_font(connector_labels[i], &lv_font_montserrat_16, LV_PART_MAIN);
		lv_obj_align(connector_labels[i], LV_ALIGN_TOP_MID, 0, 5);
		
		connector_state_labels[i] = lv_label_create(connector_panels[i]);
		lv_label_set_text(connector_state_labels[i], "State: Available");
		lv_obj_set_style_text_color(connector_state_labels[i], COLOR_AVAILABLE, LV_PART_MAIN);
		lv_obj_set_style_text_font(connector_state_labels[i], &lv_font_montserrat_14, LV_PART_MAIN);
		lv_obj_set_pos(connector_state_labels[i], 10, 35);
		
		connector_meter_labels[i] = lv_label_create(connector_panels[i]);
		lv_label_set_text(connector_meter_labels[i], "Meter: 0 Wh\nID: --\nTxn: --");
		lv_obj_set_style_text_color(connector_meter_labels[i], lv_color_hex(0xCCCCCC), LV_PART_MAIN);
		lv_obj_set_style_text_font(connector_meter_labels[i], &lv_font_montserrat_12, LV_PART_MAIN);
		lv_obj_set_pos(connector_meter_labels[i], 10, 65);
	}
	
	gui_initialized = true;
	LOG_INF("GUI initialized successfully");
	
	lv_task_handler();
	display_blanking_off(display_dev);
}

void gui_update_system_status(void)
{
	if (!gui_initialized) {
		return;
	}
	
	k_mutex_lock(&g_state_mutex, K_FOREVER);
	
	/* Update network status */
	if (g_system_status.network_connected) {
		char buf[48];
		snprintf(buf, sizeof(buf), "NET: %s", g_system_status.ip_address);
		lv_label_set_text(label_network, buf);
		lv_obj_set_style_text_color(label_network, lv_color_hex(0x00FF00), LV_PART_MAIN);
	} else {
		lv_label_set_text(label_network, "NET: Disconnected");
		lv_obj_set_style_text_color(label_network, lv_color_hex(0xFF0000), LV_PART_MAIN);
	}
	
	/* Update OCPP status */
	if (g_system_status.ocpp_connected) {
		lv_label_set_text(label_ocpp, "OCPP: Online");
		lv_obj_set_style_text_color(label_ocpp, lv_color_hex(0x00FF00), LV_PART_MAIN);
	} else {
		lv_label_set_text(label_ocpp, "OCPP: Offline");
		lv_obj_set_style_text_color(label_ocpp, lv_color_hex(0xFF0000), LV_PART_MAIN);
	}
	
	/* Update USB status */
	if (g_system_status.usb_connected) {
		lv_label_set_text(label_usb, "USB: Yes");
		lv_obj_set_style_text_color(label_usb, lv_color_hex(0x00FF00), LV_PART_MAIN);
	} else {
		lv_label_set_text(label_usb, "USB: No");
		lv_obj_set_style_text_color(label_usb, lv_color_hex(0xFF6600), LV_PART_MAIN);
	}
	
	/* Update uptime */
	char buf[32];
	uint32_t hours = g_system_status.uptime_seconds / 3600;
	uint32_t mins = (g_system_status.uptime_seconds % 3600) / 60;
	uint32_t secs = g_system_status.uptime_seconds % 60;
	snprintf(buf, sizeof(buf), "Up: %02uh%02um%02us", hours, mins, secs);
	lv_label_set_text(label_uptime, buf);
	
	/* Update CPU load */
	lv_bar_set_value(bar_cpu_load, g_system_status.cpu_load_percent, LV_ANIM_ON);
	snprintf(buf, sizeof(buf), "%u%%", g_system_status.cpu_load_percent);
	lv_label_set_text(label_cpu_load, buf);
	
	/* Color-code CPU load */
	lv_color_t cpu_color;
	if (g_system_status.cpu_load_percent < 50) {
		cpu_color = lv_color_hex(0x00FF00); /* Green */
	} else if (g_system_status.cpu_load_percent < 80) {
		cpu_color = lv_color_hex(0xFFFF00); /* Yellow */
	} else {
		cpu_color = lv_color_hex(0xFF0000); /* Red */
	}
	lv_obj_set_style_bg_color(bar_cpu_load, cpu_color, LV_PART_INDICATOR);
	lv_obj_set_style_text_color(label_cpu_load, cpu_color, LV_PART_MAIN);
	
	/* Update CPU frequency */
	snprintf(buf, sizeof(buf), "%uMHz", g_system_status.cpu_freq_mhz);
	lv_label_set_text(label_cpu_freq, buf);
	
	k_mutex_unlock(&g_state_mutex);
}

void gui_update_connector(int connector_id)
{
	if (!gui_initialized || connector_id < 0 || connector_id >= NO_OF_CONN) {
		return;
	}
	
	k_mutex_lock(&g_state_mutex, K_FOREVER);
	
	struct connector_info *conn = &g_connectors[connector_id];
	
	/* Update border color */
	lv_obj_set_style_border_color(connector_panels[connector_id],
				      get_connector_color(conn->state), LV_PART_MAIN);
	
	/* Update state label */
	char buf[64];
	snprintf(buf, sizeof(buf), "State: %s", get_connector_state_str(conn->state));
	lv_label_set_text(connector_state_labels[connector_id], buf);
	lv_obj_set_style_text_color(connector_state_labels[connector_id],
				    get_connector_color(conn->state), LV_PART_MAIN);
	
	/* Update meter and transaction info */
	snprintf(buf, sizeof(buf), "Meter: %u Wh\nID: %s\nTxn: %u",
		 conn->meter_value_wh,
		 conn->idtag[0] ? conn->idtag : "--",
		 conn->transaction_id);
	lv_label_set_text(connector_meter_labels[connector_id], buf);
	
	k_mutex_unlock(&g_state_mutex);
}

void gui_update_all(void)
{
	if (!gui_initialized) {
		return;
	}
	
	gui_update_system_status();
	
	for (int i = 0; i < NO_OF_CONN; i++) {
		gui_update_connector(i);
	}
	
	lv_task_handler();
}
