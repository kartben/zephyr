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
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/usb/class/usbd_midi2.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/display/cfb.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_usb_midi, LOG_LEVEL_INF);

static const struct device *const midi = DEVICE_DT_GET(DT_NODELABEL(usb_midi));
static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const led_strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* Buffer for LED strip pixels - MacroPad has 12 RGB LEDs */
static struct led_rgb pixels[12];

static void display_init(void)
{
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO10) != 0) {
		if (display_set_pixel_format(display_dev, PIXEL_FORMAT_MONO01) != 0) {
			LOG_ERR("Failed to set required pixel format");
			return;
		}
	}

	if (cfb_framebuffer_init(display_dev)) {
		LOG_ERR("Framebuffer initialization failed!");
		return;
	}

	cfb_framebuffer_clear(display_dev, true);
	display_blanking_off(display_dev);
	cfb_framebuffer_set_font(display_dev, 0);
	LOG_INF("Display initialized");
}

static void key_press(struct input_event *evt, void *user_data)
{
	/* Handle relative input events as pitch bend */
	if (evt->type == INPUT_EV_REL) {
		/* Scale relative movement (-128 to 127) to pitch bend range (0 to 16383) */
		static int16_t pitch_bend = 8192; /* Center position */
		pitch_bend += (evt->value * 128); /* Scale the movement */

		/* Clamp to valid range */
		if (pitch_bend > 16383) {
			pitch_bend = 16383;
		} else if (pitch_bend < 0) {
			pitch_bend = 0;
		}

		/* Send pitch bend message */
		uint8_t lsb = pitch_bend & 0x7F;
		uint8_t msb = (pitch_bend >> 7) & 0x7F;
		struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(0, UMP_MIDI_PITCH_BEND, 0, lsb, msb);
		usbd_midi_send(midi, ump);

		/* Display pitch bend value if display is available */
		if (device_is_ready(display_dev)) {
			char msg[32];
			snprintf(msg, sizeof(msg), "Pitch: %d", pitch_bend);
			cfb_framebuffer_clear(display_dev, false);
			cfb_print(display_dev, msg, 0, 0);
			cfb_framebuffer_finalize(display_dev);
		}
		return;
	}

	/* Only handle key presses */
	if (evt->type != INPUT_EV_KEY) {
		return;
	}

	/* Map F1-F12 to LED indices 0-11 */
	size_t led_idx;
	if (evt->code >= INPUT_KEY_F1 && evt->code <= INPUT_KEY_F10) {
		led_idx = evt->code - INPUT_KEY_F1;
	} else if (evt->code >= INPUT_KEY_F11 && evt->code <= INPUT_KEY_F12) {
		led_idx = evt->code - INPUT_KEY_F11 + 10;
	} else {
		return; /* Ignore other keys */
	}

	uint8_t command = evt->value ? UMP_MIDI_NOTE_ON : UMP_MIDI_NOTE_OFF;
	uint8_t channel = 0;
	uint8_t note = 60 + led_idx; /* Map to middle C (60) and up */
	uint8_t velocity = 127;      /* Full velocity for bright LEDs */

	/* Update LED strip */
	if (device_is_ready(led_strip)) {
		if (evt->value) {
			/* Key pressed - light up corresponding LED */
			pixels[led_idx].r = 0;
			pixels[led_idx].g = 255; /* Bright green */
			pixels[led_idx].b = 0;
		} else {
			/* Key released - turn off LED */
			pixels[led_idx].r = 0;
			pixels[led_idx].g = 0;
			pixels[led_idx].b = 0;
		}
		led_strip_update_rgb(led_strip, pixels, 12);
	}

	/* Toggle LED based on key state */
	if (led.port) {
		gpio_pin_set_dt(&led, evt->value);
	}

	/* Display key press information */
	if (device_is_ready(display_dev)) {
		char msg[32];
		char ret_msg[32];
		snprintf(msg, sizeof(msg), "Key F%d: %s", led_idx + 1, evt->value ? "ON" : "OFF");
		cfb_framebuffer_clear(display_dev, false);
		cfb_print(display_dev, msg, 0, 0);

		struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(0, command, channel, note, velocity);
		int ret = usbd_midi_send(midi, ump);
		snprintf(ret_msg, sizeof(ret_msg), "Send ret: %d", ret);
		cfb_print(display_dev, ret_msg, 0, 20);

		/* Display UMP bytes */
		snprintf(ret_msg, sizeof(ret_msg), "%02X%02X%02X%02X", (uint8_t)(ump.data[0] >> 24),
			 (uint8_t)(ump.data[0] >> 16), (uint8_t)(ump.data[0] >> 8),
			 (uint8_t)ump.data[0]);
		cfb_print(display_dev, ret_msg, 0, 40);

		cfb_framebuffer_finalize(display_dev);
		return;
	}

	struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(0, command, channel, note, velocity);
	usbd_midi_send(midi, ump);
}
INPUT_CALLBACK_DEFINE(NULL, key_press, NULL);

static void on_midi_packet(const struct device *dev, const struct midi_ump ump)
{
	LOG_INF("Received MIDI packet (MT=%X)", UMP_MT(ump));

	/* Only send MIDI1 channel voice messages back to the host */
	if (UMP_MT(ump) == UMP_MT_MIDI1_CHANNEL_VOICE) {
		LOG_INF("Send back MIDI1 message %02X %02X %02X", UMP_MIDI_STATUS(ump),
			UMP_MIDI1_P1(ump), UMP_MIDI1_P2(ump));
		// usbd_midi_send(dev, ump);
	}
}

static void on_device_ready(const struct device *dev, const bool ready)
{
	/* Light up the LED (if any) when USB-MIDI2.0 is enabled */
	if (led.port) {
		gpio_pin_set_dt(&led, ready);
	}
}

static const struct usbd_midi_ops ops = {
	.rx_packet_cb = on_midi_packet,
	.ready_cb = on_device_ready,
};

int main(void)
{
	struct usbd_context *sample_usbd;

	if (!device_is_ready(midi)) {
		LOG_ERR("MIDI device not ready");
		return -1;
	}

	if (!device_is_ready(led_strip)) {
		LOG_ERR("LED strip device not ready");
		return -1;
	}

	if (led.port) {
		if (gpio_pin_configure_dt(&led, GPIO_OUTPUT)) {
			LOG_ERR("Unable to setup LED, not using it");
			memset(&led, 0, sizeof(led));
		}
	}

	/* Initialize display */
	display_init();

	usbd_midi_set_ops(midi, &ops);

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		LOG_ERR("Failed to initialize USB device");
		return -1;
	}

	if (usbd_enable(sample_usbd)) {
		LOG_ERR("Failed to enable device support");
		return -1;
	}

	LOG_INF("USB device support enabled");
	return 0;
}
