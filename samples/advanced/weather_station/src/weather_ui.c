/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "weather_internal.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(weather_ui, CONFIG_LOG_DEFAULT_LEVEL);

static void style_card(lv_obj_t *card, lv_color_t bg_color)
{
	lv_obj_set_style_bg_color(card, bg_color, LV_PART_MAIN);
	lv_obj_set_style_bg_opa(card, LV_OPA_90, LV_PART_MAIN);
	lv_obj_set_style_border_width(card, 1, LV_PART_MAIN);
	lv_obj_set_style_border_color(card, lv_color_hex(0x3d5a80), LV_PART_MAIN);
	lv_obj_set_style_radius(card, 14, LV_PART_MAIN);
	lv_obj_set_style_pad_all(card, 10, LV_PART_MAIN);
}

static void weather_ui_apply_layout(struct weather_ui *ui, int16_t width, int16_t height)
{
	const int16_t margin = 8;
	const int16_t gap = 8;
	bool landscape = width >= CONFIG_SAMPLE_WEATHER_UI_LANDSCAPE_BREAKPOINT;

	int16_t header_h = (height * 28) / 100;
	if (header_h < 64) {
		header_h = 64;
	}

	if (landscape) {
		int16_t metrics_h = (height * 34) / 100;
		int16_t status_h;
		int16_t card_w;

		if (metrics_h < 68) {
			metrics_h = 68;
		}

		status_h = height - (margin * 2) - header_h - metrics_h - (gap * 2);
		if (status_h < 56) {
			status_h = 56;
		}

		card_w = (width - (margin * 2) - gap) / 2;

		lv_obj_set_pos(ui->header_card, margin, margin);
		lv_obj_set_size(ui->header_card, width - (margin * 2), header_h);

		lv_obj_set_pos(ui->temp_card, margin, margin + header_h + gap);
		lv_obj_set_size(ui->temp_card, card_w, metrics_h);

		lv_obj_set_pos(ui->humidity_card, margin + card_w + gap, margin + header_h + gap);
		lv_obj_set_size(ui->humidity_card, card_w, metrics_h);

		lv_obj_set_pos(ui->status_card, margin, margin + header_h + gap + metrics_h + gap);
		lv_obj_set_size(ui->status_card, width - (margin * 2), status_h);
	} else {
		int16_t available = height - (margin * 2) - header_h - (gap * 3);
		int16_t card_h = available / 3;

		if (card_h < 56) {
			card_h = 56;
		}

		lv_obj_set_pos(ui->header_card, margin, margin);
		lv_obj_set_size(ui->header_card, width - (margin * 2), header_h);

		lv_obj_set_pos(ui->temp_card, margin, margin + header_h + gap);
		lv_obj_set_size(ui->temp_card, width - (margin * 2), card_h);

		lv_obj_set_pos(ui->humidity_card, margin, margin + header_h + gap + card_h + gap);
		lv_obj_set_size(ui->humidity_card, width - (margin * 2), card_h);

		lv_obj_set_pos(ui->status_card, margin,
			       margin + header_h + gap + card_h + gap + card_h + gap);
		lv_obj_set_size(ui->status_card, width - (margin * 2), card_h);
	}

	lv_obj_align(ui->title_label, LV_ALIGN_TOP_LEFT, 0, 0);
	lv_obj_align(ui->clock_source_label, LV_ALIGN_TOP_RIGHT, 0, 0);
	lv_obj_align(ui->time_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);
	lv_obj_set_width(ui->status_label, lv_obj_get_width(ui->status_card) - 8);
}

int weather_ui_init(struct weather_ui *ui)
{
	lv_obj_t *label;

	if (ui == NULL) {
		return -EINVAL;
	}

	memset(ui, 0, sizeof(*ui));
	ui->screen = lv_screen_active();
	if (ui->screen == NULL) {
		return -ENODEV;
	}

	lv_obj_set_style_bg_color(ui->screen, lv_color_hex(0x0d1b2a), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_color(ui->screen, lv_color_hex(0x1b263b), LV_PART_MAIN);
	lv_obj_set_style_bg_grad_dir(ui->screen, LV_GRAD_DIR_VER, LV_PART_MAIN);

	ui->header_card = lv_obj_create(ui->screen);
	ui->temp_card = lv_obj_create(ui->screen);
	ui->humidity_card = lv_obj_create(ui->screen);
	ui->status_card = lv_obj_create(ui->screen);

	style_card(ui->header_card, lv_color_hex(0x243b55));
	style_card(ui->temp_card, lv_color_hex(0x2a4d69));
	style_card(ui->humidity_card, lv_color_hex(0x1f6f8b));
	style_card(ui->status_card, lv_color_hex(0x324a5f));

	lv_obj_set_flex_flow(ui->temp_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui->temp_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_flex_flow(ui->humidity_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui->humidity_card, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER,
			      LV_FLEX_ALIGN_CENTER);
	lv_obj_set_flex_flow(ui->status_card, LV_FLEX_FLOW_COLUMN);
	lv_obj_set_flex_align(ui->status_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START,
			      LV_FLEX_ALIGN_START);

	ui->title_label = lv_label_create(ui->header_card);
	lv_label_set_text(ui->title_label, "Weather Station");
	lv_obj_set_style_text_font(ui->title_label, &lv_font_montserrat_20, LV_PART_MAIN);

	ui->clock_source_label = lv_label_create(ui->header_card);
	lv_label_set_text(ui->clock_source_label, "Clock: --");

	ui->time_label = lv_label_create(ui->header_card);
	lv_label_set_text(ui->time_label, "time pending");
	lv_obj_set_style_text_font(ui->time_label, &lv_font_montserrat_20, LV_PART_MAIN);

	label = lv_label_create(ui->temp_card);
	lv_label_set_text(label, "Temperature");
	ui->temp_value_label = lv_label_create(ui->temp_card);
	lv_label_set_text(ui->temp_value_label, "--.- C");
	lv_obj_set_style_text_font(ui->temp_value_label, &lv_font_montserrat_28, LV_PART_MAIN);

	label = lv_label_create(ui->humidity_card);
	lv_label_set_text(label, "Humidity");
	ui->humidity_value_label = lv_label_create(ui->humidity_card);
	lv_label_set_text(ui->humidity_value_label, "--.- %RH");
	lv_obj_set_style_text_font(ui->humidity_value_label, &lv_font_montserrat_28, LV_PART_MAIN);

	ui->status_label = lv_label_create(ui->status_card);
	lv_label_set_long_mode(ui->status_label, LV_LABEL_LONG_WRAP);
	lv_label_set_text(ui->status_label, "Status pending");

	ui->last_width = -1;
	ui->last_height = -1;

	return 0;
}

void weather_ui_update(struct weather_ui *ui, const struct weather_model *model)
{
	char time_buf[48];
	char temp_buf[24];
	char humidity_buf[24];
	char status_buf[196];
	int16_t width;
	int16_t height;

	width = lv_obj_get_width(ui->screen);
	height = lv_obj_get_height(ui->screen);

	if (width != ui->last_width || height != ui->last_height) {
		weather_ui_apply_layout(ui, width, height);
		ui->last_width = width;
		ui->last_height = height;
	}

	weather_model_format_clock(model, time_buf, sizeof(time_buf));
	lv_label_set_text(ui->time_label, time_buf);

	snprintk(temp_buf, sizeof(temp_buf), "Clock: %s", weather_model_clock_source_text(model));
	lv_label_set_text(ui->clock_source_label, temp_buf);

	weather_model_format_temperature(model, temp_buf, sizeof(temp_buf));
	snprintk(humidity_buf, sizeof(humidity_buf), "%s %s", temp_buf,
		 weather_model_temperature_unit());
	lv_label_set_text(ui->temp_value_label, humidity_buf);

	weather_model_format_humidity(model, humidity_buf, sizeof(humidity_buf));
	snprintk(temp_buf, sizeof(temp_buf), "%s %%RH", humidity_buf);
	lv_label_set_text(ui->humidity_value_label, temp_buf);

	snprintk(status_buf, sizeof(status_buf),
		 "Sensor: %s (err %d)\nLast sample: %lld ms\nBLE: %s",
		 model->measurements_valid ? "ok" : "waiting", model->last_sensor_error,
		 (long long)model->last_sample_ms, weather_ble_status_string());
	lv_label_set_text(ui->status_label, status_buf);
}
