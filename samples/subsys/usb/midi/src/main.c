/*
 * Copyright (c) 2024 Titouan Christophe
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * @file
 * @brief Sample application for USB MIDI 2.0 device class
 */

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/usb/class/usb_midi.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_usb_midi, LOG_LEVEL_INF);

static const struct device *const midi = DEVICE_DT_GET(DT_NODELABEL(usb_midi));

/* On boards that have a user button; button press/release -> MIDI note on/off */
#if DT_NODE_EXISTS(DT_NODELABEL(user_button))
static const struct device *const button = DEVICE_DT_GET(DT_PARENT(DT_NODELABEL(user_button)));

static void key_press(struct input_event *evt, void *user_data)
{
	uint8_t command = evt->value ? MIDI_NOTE_ON : MIDI_NOTE_OFF;

	usb_midi_send(midi, &UMP_MIDI1(0, command, 0, 0x40, 0x7f));
}
INPUT_CALLBACK_DEFINE(button, key_press, NULL);
#endif

static void on_midi_packet(const struct device *dev, const union ump *midi_packet)
{
	LOG_INF("Received MIDI packet (MT=%X)", midi_packet->mt);

	/* Only send MIDI1 channel voice messages back to the host */
	if (midi_packet->mt == MT_MIDI1_CHANNEL_VOICE) {
		const struct ump_midi1 *midi1 = &midi_packet->midi1;

		LOG_INF("Send back MIDI1 message chan=%d status=%d %02X %02X", midi1->channel,
			midi1->status, midi1->p1, midi1->p2);
		usb_midi_send(dev, midi_packet);
	}
}



#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>

static uint32_t count;

#ifdef CONFIG_GPIO
static struct gpio_dt_spec button_gpio = GPIO_DT_SPEC_GET_OR(
		DT_ALIAS(sw0), gpios, {0});
static struct gpio_callback button_callback;

static void button_isr_callback(const struct device *port,
				struct gpio_callback *cb,
				uint32_t pins)
{
	ARG_UNUSED(port);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	count = 0;
}
#endif /* CONFIG_GPIO */

#ifdef CONFIG_LV_Z_ENCODER_INPUT
static const struct device *lvgl_encoder =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_lvgl_encoder_input));
#endif /* CONFIG_LV_Z_ENCODER_INPUT */

#ifdef CONFIG_LV_Z_KEYPAD_INPUT
static const struct device *lvgl_keypad =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(zephyr_lvgl_keypad_input));
#endif /* CONFIG_LV_Z_KEYPAD_INPUT */

static void lv_btn_click_callback(lv_event_t *e)
{
	printk("Key clicked\n");
}

// Callback for button events
void key_event_cb(lv_event_t * e) {
    lv_obj_t * key = lv_event_get_target(e);
    const char * key_name = lv_obj_get_user_data(key);
    uint32_t * pitch = lv_event_get_user_data(e);


    if (e->code == LV_EVENT_PRESSED) {
	printf("Key pressed - color: %s, pitch: %d\n", key_name, pitch);
        lv_obj_set_style_bg_color(key, lv_color_hex(0xAAAAAA), LV_PART_MAIN); // Highlight the key
        usb_midi_send(midi, &UMP_MIDI1(0, MIDI_NOTE_ON, 0, pitch, 0x7f));
    } else if (e->code == LV_EVENT_RELEASED) {
        lv_color_t default_color = strcmp(key_name, "black") == 0 ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF);
        lv_obj_set_style_bg_color(key, default_color, LV_PART_MAIN); // Reset the color
	usb_midi_send(midi, &UMP_MIDI1(0, MIDI_NOTE_OFF, 0, pitch, 0x7f));
    }
}

/* white key notes */
static const int32_t white_key_notes[] = {
	    60, 62, 64, 65, 67, 69, 71
};

/* black key notes */
static const int32_t black_key_notes[] = {
	    61, 63, 0, 66, 68, 70, 0
};

void draw_piano_keyboard(lv_obj_t * parent) {
    const lv_coord_t screen_width = 320;
    const lv_coord_t screen_height = 240;

    // Dimensions for white keys
    const lv_coord_t white_key_width = screen_width / 7; // Divide the screen width by 7 keys
    const lv_coord_t white_key_height = screen_height;

    // Dimensions for black keys
    const lv_coord_t black_key_width = white_key_width * 0.6; // Black keys are narrower
    const lv_coord_t black_key_height = screen_height * 0.6; // Black keys are shorter

    // Draw white keys
    for (int i = 0; i < 7; i++) {
        lv_obj_t * white_key = lv_btn_create(parent);
        lv_obj_set_size(white_key, white_key_width, white_key_height);
        lv_obj_set_pos(white_key, i * white_key_width, 0);
        lv_obj_set_style_bg_color(white_key, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
        lv_obj_set_style_border_width(white_key, 1, LV_PART_MAIN);
        lv_obj_set_user_data(white_key, "white");
        lv_obj_add_event_cb(white_key, key_event_cb, LV_EVENT_ALL, (void*)(intptr_t)white_key_notes[i]); // set pitch as user data
    }

    // Draw black keys
    for (int i = 0; i < 7; i++) {
        if (i == 2 || i == 6) continue; // No black keys between B/C and E/F

        lv_obj_t * black_key = lv_btn_create(parent);
        lv_obj_set_size(black_key, black_key_width, black_key_height);
        lv_obj_set_pos(black_key, (i + 1) * white_key_width - (black_key_width / 2), 0);
        lv_obj_set_style_bg_color(black_key, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_width(black_key, 1, LV_PART_MAIN);
        lv_obj_set_user_data(black_key, "black");
        lv_obj_add_event_cb(black_key, key_event_cb, LV_EVENT_ALL, (void*)(intptr_t)black_key_notes[i]); // set pitch as user data
    }
}

int main(void)
{
	char count_str[11] = {0};
	const struct device *display_dev;
	lv_obj_t *hello_world_label;
	lv_obj_t *count_label;

	struct usbd_context *sample_usbd;

	if (!device_is_ready(midi)) {
		LOG_ERR("MIDI device not ready");
		return -1;
	}

	usb_midi_set_callback(midi, on_midi_packet);

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -1;
	}

	if (!usbd_can_detect_vbus(sample_usbd) && usbd_enable(sample_usbd)) {
		LOG_ERR("Failed to enable device support");
		return -1;
	}

	LOG_INF("USB device support enabled");

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	draw_piano_keyboard(lv_scr_act());

	lv_task_handler();
	display_blanking_off(display_dev);

	while (1) {
		lv_task_handler();
		++count;
		k_sleep(K_MSEC(10));
	}
}









