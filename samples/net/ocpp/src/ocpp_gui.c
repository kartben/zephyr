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
#include "ocpp_gui.h"

LOG_MODULE_REGISTER(ocpp_gui, LOG_LEVEL_INF);

/* GUI Elements */
static lv_obj_t *main_screen;
static lv_obj_t *status_label;
static lv_obj_t *connector1_panel;
static lv_obj_t *connector2_panel;
static lv_obj_t *connector1_status;
static lv_obj_t *connector1_energy;
static lv_obj_t *connector1_power;
static lv_obj_t *connector2_status;
static lv_obj_t *connector2_energy;
static lv_obj_t *connector2_power;
static lv_obj_t *cs_status_label;

/* Color definitions for modern look */
#define COLOR_PRIMARY     0x2196F3  /* Blue */
#define COLOR_SUCCESS     0x4CAF50  /* Green */
#define COLOR_WARNING     0xFF9800  /* Orange */
#define COLOR_DANGER      0xF44336  /* Red */
#define COLOR_BACKGROUND  0x212121  /* Dark Gray */
#define COLOR_SURFACE     0x424242  /* Medium Gray */
#define COLOR_TEXT        0xFFFFFF  /* White */

static lv_obj_t *create_connector_panel(lv_obj_t *parent, const char *title, int y_offset)
{
	lv_obj_t *panel = lv_obj_create(parent);
	lv_obj_set_size(panel, 280, 140);
	lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, y_offset);
	lv_obj_set_style_bg_color(panel, lv_color_hex(COLOR_SURFACE), 0);
	lv_obj_set_style_border_width(panel, 2, 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_set_style_radius(panel, 10, 0);
	lv_obj_set_style_pad_all(panel, 12, 0);

	/* Title */
	lv_obj_t *title_label = lv_label_create(panel);
	lv_label_set_text(title_label, title);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_20, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, 0, 0);

	return panel;
}

static lv_obj_t *create_info_label(lv_obj_t *parent, const char *text, int y_offset)
{
	lv_obj_t *label = lv_label_create(parent);
	lv_label_set_text(label, text);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(COLOR_TEXT), 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 0, y_offset);
	return label;
}

void ocpp_gui_init(void)
{
	const struct device *display_dev;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	/* Create main screen with dark theme */
	main_screen = lv_screen_active();
	lv_obj_set_style_bg_color(main_screen, lv_color_hex(COLOR_BACKGROUND), 0);

	/* Header */
	lv_obj_t *header = lv_label_create(main_screen);
	lv_label_set_text(header, "OCPP Charging Station");
	lv_obj_set_style_text_font(header, &lv_font_montserrat_24, 0);
	lv_obj_set_style_text_color(header, lv_color_hex(COLOR_PRIMARY), 0);
	lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 10);

	/* Central System Status */
	cs_status_label = lv_label_create(main_screen);
	lv_label_set_text(cs_status_label, "CS: Disconnected");
	lv_obj_set_style_text_font(cs_status_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(cs_status_label, lv_color_hex(COLOR_DANGER), 0);
	lv_obj_align(cs_status_label, LV_ALIGN_TOP_MID, 0, 40);

	/* Connector 1 Panel */
	connector1_panel = create_connector_panel(main_screen, "Connector 1", 70);
	connector1_status = create_info_label(connector1_panel, "Status: Available", 30);
	connector1_energy = create_info_label(connector1_panel, "Energy: 0 Wh", 55);
	connector1_power = create_info_label(connector1_panel, "Power: 0 W", 80);

	/* Connector 2 Panel */
	connector2_panel = create_connector_panel(main_screen, "Connector 2", 220);
	connector2_status = create_info_label(connector2_panel, "Status: Available", 30);
	connector2_energy = create_info_label(connector2_panel, "Energy: 0 Wh", 55);
	connector2_power = create_info_label(connector2_panel, "Power: 0 W", 80);

	/* Status bar at bottom */
	status_label = lv_label_create(main_screen);
	lv_label_set_text(status_label, "Ready");
	lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
	lv_obj_set_style_text_color(status_label, lv_color_hex(0x9E9E9E), 0);
	lv_obj_align(status_label, LV_ALIGN_BOTTOM_MID, 0, -5);

	lv_timer_handler();
	display_blanking_off(display_dev);

	LOG_INF("OCPP GUI initialized");
}

void ocpp_gui_update_cs_status(bool connected)
{
	if (cs_status_label) {
		if (connected) {
			lv_label_set_text(cs_status_label, "CS: Connected");
			lv_obj_set_style_text_color(cs_status_label, lv_color_hex(COLOR_SUCCESS), 0);
		} else {
			lv_label_set_text(cs_status_label, "CS: Disconnected");
			lv_obj_set_style_text_color(cs_status_label, lv_color_hex(COLOR_DANGER), 0);
		}
	}
}

void ocpp_gui_update_connector_status(int connector_id, const char *status)
{
	lv_obj_t *status_obj = (connector_id == 1) ? connector1_status : connector2_status;
	lv_obj_t *panel = (connector_id == 1) ? connector1_panel : connector2_panel;
	
	if (status_obj && panel) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Status: %s", status);
		lv_label_set_text(status_obj, buf);

		/* Update panel border color based on status */
		lv_color_t border_color;
		if (strcmp(status, "Charging") == 0) {
			border_color = lv_color_hex(COLOR_SUCCESS);
		} else if (strcmp(status, "Preparing") == 0 || strcmp(status, "Finishing") == 0) {
			border_color = lv_color_hex(COLOR_WARNING);
		} else {
			border_color = lv_color_hex(COLOR_PRIMARY);
		}
		lv_obj_set_style_border_color(panel, border_color, 0);
	}
}

void ocpp_gui_update_connector_energy(int connector_id, int energy_wh)
{
	lv_obj_t *energy_obj = (connector_id == 1) ? connector1_energy : connector2_energy;
	
	if (energy_obj) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Energy: %d Wh", energy_wh);
		lv_label_set_text(energy_obj, buf);
	}
}

void ocpp_gui_update_connector_power(int connector_id, int power_w)
{
	lv_obj_t *power_obj = (connector_id == 1) ? connector1_power : connector2_power;
	
	if (power_obj) {
		char buf[64];
		snprintf(buf, sizeof(buf), "Power: %d W", power_w);
		lv_label_set_text(power_obj, buf);
	}
}

void ocpp_gui_update_status(const char *status_text)
{
	if (status_label) {
		lv_label_set_text(status_label, status_text);
	}
}

void ocpp_gui_task(void)
{
	lv_timer_handler();
}
