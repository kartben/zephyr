/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Example: Musical Instrument UI using rotary encoder and LVGL.
 * This sample cycles through instrument functions on encoder click and adjusts
 * parameter values on left/right turns.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

/* Define instrument functions */
enum instrument_mode {
	MODE_WAVEFORM,
	MODE_VOLUME,
	MODE_FILTER,
	MODE_MIDI_CHANNEL,
	MODE_OCTAVE,
	MODE_COUNT
};

static const char *mode_names[MODE_COUNT] = {
	"Waveform", // Could be encoded as 0=sine, 1=square, etc.
	"Volume",   // 0-100%
	"Filter",   // Cutoff frequency in Hz
	"MIDI Ch.", // MIDI channel 1-16
	"Octave"    // Offset value, e.g. -2..+2
};

/* Initial parameter values */
static int mode_values[MODE_COUNT] = {
	0,    /* Waveform (0: sine, 1: square, etc.) */
	50,   /* Volume */
	1000, /* Filter cutoff */
	1,    /* MIDI channel */
	4     /* Octave (this value is arbitrary, adjust as needed) */
};

static uint8_t current_mode = 0;
static lv_obj_t *ui_label;

/* Update the label text to show current function and value */
static void update_ui(void)
{
	char buf[64];
	snprintf(buf, sizeof(buf), "%s: %d", mode_names[current_mode], mode_values[current_mode]);
	lv_label_set_text(ui_label, buf);
}

/* Event callback for our instrument UI label */
static void instrument_event_cb(lv_event_t *e)
{
	printk("Instrument event callback\n");
	lv_event_code_t code = lv_event_get_code(e);

	/* We assume the rotary encoder sends key events:
	 * LV_KEY_ENTER for a click,
	 * LV_KEY_RIGHT/LEFT for turning.
	 */
	if (code == LV_EVENT_KEY) {
		printk("Key event\n");
		uint32_t key = *((uint32_t *)lv_event_get_param(e));
		printk("Key: %d ENTER=%d RIGHT=%d LEFT=%d\n", key, LV_KEY_ENTER, LV_KEY_RIGHT,
		       LV_KEY_LEFT);

		if (key == LV_KEY_ENTER) {
			/* Cycle to the next function */
			current_mode = (current_mode + 1) % MODE_COUNT;
			update_ui();
		} else if (key == LV_KEY_RIGHT) {
			/* Increase current parameter value */
			mode_values[current_mode]++;
			update_ui();
		} else if (key == LV_KEY_LEFT) {
			/* Decrease current parameter value */
			mode_values[current_mode]--;
			update_ui();
		}
	}
}

int main(void)
{
	const struct device *display_dev;

	/* Get and check the display device */
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return 0;
	}

	/* Initialize LVGL (assumes LVGL is already configured) */
	lv_init();

	/* If using an LVGL buffer and driver, initialize those here */
	/* ... (display and input device driver initialization) ... */

	/* Create a simple label for our instrument UI */
	ui_label = lv_label_create(lv_scr_act());
	lv_obj_align(ui_label, LV_ALIGN_CENTER, 0, 0);
	update_ui();

	/* Register the event callback to listen for key events on ui_label */
	lv_obj_add_event_cb(ui_label, instrument_event_cb, LV_EVENT_KEY, NULL);

	/* Set up an input group so that the rotary encoder events are directed to ui_label.
	 * (Assumes your lvgl_input_device integration maps the rotary encoder to LVGL keys)
	 */
#ifdef CONFIG_LV_Z_ENCODER_INPUT
	{
		printk("Setting up input group\n");
		lv_group_t *group = lv_group_create();
		lv_group_add_obj(group, ui_label);
		/* Get the LVGL input device for the encoder */
		const struct device *lvgl_encoder =
			DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_lvgl_encoder_input));
		if (lvgl_encoder != NULL) {
			lv_indev_set_group(lvgl_input_get_indev(lvgl_encoder), group);
		} else {
			LOG_ERR("Encoder input device not found");
		}
	}
#endif

	/* Turn off display blanking */
	display_blanking_off(display_dev);

	/* Main loop */
	while (1) {
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}

	return 0;
}
