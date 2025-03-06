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
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/ring_buffer.h>
#include <zephyr/random/random.h>
#include <lvgl.h>
#include <stdio.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(sample_usb_midi, LOG_LEVEL_INF);

static const struct device *const midi = DEVICE_DT_GET(DT_NODELABEL(usb_midi));
static const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const struct device *const led_strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static struct gpio_dt_spec led = GPIO_DT_SPEC_GET_OR(DT_ALIAS(led0), gpios, {0});

/* Buffer for LED strip pixels - MacroPad has 12 RGB LEDs */
static struct led_rgb pixels[12];

/* Arpeggiator States */
enum arp_states {
	ARP_OFF,
	ARP_UP,
	ARP_DOWN,
	ARP_UP_DOWN,
	ARP_DOWN_UP,
	ARP_RANDOM,
	ARP_STATE_COUNT
};

static const char *arp_state_names[] = {"Off", "Up", "Down", "Up&Down", "Down&Up", "Random"};

/* Arpeggiator Configuration */
struct arpeggiator {
	enum arp_states state;
	int32_t bpm;
	uint8_t current_step;
	uint8_t last_note;
	bool ascending; /* Direction for up/down patterns */
	struct k_timer timer;
	struct k_work work;
	struct ring_buf note_buf;
	uint8_t note_buffer[16];  /* Stores up to 16 notes */
	uint8_t sorted_notes[16]; /* Sorted copy of notes for patterns */
	uint8_t note_count;
};

/* Global Instance */
static struct arpeggiator arp;

/* Define instrument functions */
enum instrument_mode {
	MODE_VOLUME,
	MODE_OCTAVE,
	MODE_ARP_STATE,
	MODE_ARP_BPM,
	MODE_SCALE,     // New scale mode
	MODE_ROOT_NOTE, // New root note mode
	MODE_REVERB,    // New reverb mode
	MODE_JOG,       // New jog mode
	MODE_COUNT
};

static const char *mode_names[MODE_COUNT] = {"Volume", "Octave",    "Arp Mode", "Arp BPM",
					     "Scale",  "Root Note", "Reverb",   "Jog"};

/* Scale definitions */
enum scale_types {
	SCALE_CHROMATIC,
	SCALE_MAJOR,
	SCALE_NATURAL_MINOR,
	SCALE_HARMONIC_MINOR,
	SCALE_PENTATONIC_MAJOR,
	SCALE_PENTATONIC_MINOR,
	SCALE_COUNT
};

static const char *scale_names[] = {"Chromatic",      "Major",       "Natural Minor",
				    "Harmonic Minor", "Pent. Major", "Pent. Minor"};

static const uint8_t SCALES[][12] = {
	{1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}, // Chromatic (all notes)
	{1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1}, // Major
	{1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0}, // Natural Minor
	{1, 0, 1, 1, 0, 1, 0, 1, 1, 0, 0, 1}, // Harmonic Minor
	{1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 0}, // Pentatonic Major
	{1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 0}  // Pentatonic Minor
};

/* Initial parameter values */
static int mode_values[MODE_COUNT] = {
	100, /* Volume */
	0,   /* Octave */
	0,   /* Arpeggiator state */
	120, /* Arpeggiator BPM */
	0,   /* Scale type (Chromatic) */
	0,   /* Root note (C) */
	0,   /* Reverb (0-127) */
	0    /* Jog (0 = stopped) */
};

static uint8_t current_mode = 0;
static lv_obj_t *ui_label;

/* Forward declarations */
static void send_midi_note(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on);
static void send_midi_cc(uint8_t channel, uint8_t controller, uint8_t value);

/* Update the label text to show current function and value */
static void update_ui(void)
{
	char buf[64];
	if (current_mode == MODE_ARP_STATE) {
		snprintf(buf, sizeof(buf), "%s: %s", mode_names[current_mode],
			 arp_state_names[mode_values[current_mode]]);
	} else if (current_mode == MODE_SCALE) {
		snprintf(buf, sizeof(buf), "%s: %s", mode_names[current_mode],
			 scale_names[mode_values[current_mode]]);
	} else if (current_mode == MODE_ROOT_NOTE) {
		const char *note_names[] = {"C",  "C#", "D",  "D#", "E",  "F",
					    "F#", "G",  "G#", "A",  "A#", "B"};
		snprintf(buf, sizeof(buf), "%s: %s", mode_names[current_mode],
			 note_names[mode_values[current_mode]]);
	} else {
		snprintf(buf, sizeof(buf), "%s: %d", mode_names[current_mode],
			 mode_values[current_mode]);
	}
	lv_label_set_text(ui_label, buf);
}

/* Sort notes in ascending order */
static void sort_arp_notes(void)
{
	uint8_t temp_buf[16];
	uint32_t bytes_read = ring_buf_get(&arp.note_buf, temp_buf, sizeof(temp_buf));
	arp.note_count = bytes_read;

	/* Copy notes to sorted array */
	memcpy(arp.sorted_notes, temp_buf, bytes_read);

	/* Put notes back in ring buffer */
	ring_buf_put(&arp.note_buf, temp_buf, bytes_read);

	/* Simple bubble sort */
	for (int i = 0; i < arp.note_count - 1; i++) {
		for (int j = 0; j < arp.note_count - i - 1; j++) {
			if (arp.sorted_notes[j] > arp.sorted_notes[j + 1]) {
				uint8_t temp = arp.sorted_notes[j];
				arp.sorted_notes[j] = arp.sorted_notes[j + 1];
				arp.sorted_notes[j + 1] = temp;
			}
		}
	}
}

static void add_note(uint8_t note)
{
	if (ring_buf_space_get(&arp.note_buf) >= sizeof(note)) {
		ring_buf_put(&arp.note_buf, &note, sizeof(note));
		sort_arp_notes(); /* Resort when adding notes */
	}
}

static void remove_note(uint8_t note)
{
	uint8_t temp_buf[16];
	uint32_t bytes_read =
		ring_buf_get(&arp.note_buf, temp_buf, ring_buf_size_get(&arp.note_buf));
	uint32_t note_count = bytes_read;

	/* Clear the buffer */
	ring_buf_reset(&arp.note_buf);

	/* Add back all notes except the one to remove */
	for (uint32_t i = 0; i < note_count; i++) {
		if (temp_buf[i] != note) {
			ring_buf_put(&arp.note_buf, &temp_buf[i], sizeof(note));
		}
	}

	/* Resort remaining notes */
	sort_arp_notes();

	/* Reset step and direction if buffer is empty */
	if (ring_buf_is_empty(&arp.note_buf)) {
		arp.current_step = 0;
		arp.ascending = true;
	}
}

/* Send MIDI note with current parameters */
static void send_midi_note(uint8_t channel, uint8_t note, uint8_t velocity, bool note_on)
{
	LOG_INF("Sending MIDI %s: note=%d, velocity=%d", note_on ? "NOTE ON" : "NOTE OFF", note,
		velocity);
	uint8_t command = note_on ? UMP_MIDI_NOTE_ON : UMP_MIDI_NOTE_OFF;
	struct midi_ump ump = UMP_MIDI1_CHANNEL_VOICE(0, command, 0, note, velocity);
	usbd_midi_send(midi, ump);
}

/* Send MIDI Control Change message */
static void send_midi_cc(uint8_t channel, uint8_t controller, uint8_t value)
{
	struct midi_ump ump =
		UMP_MIDI1_CHANNEL_VOICE(0, UMP_MIDI_CONTROL_CHANGE, 0, controller, value);
	usbd_midi_send(midi, ump);
}

/* Timer Callback */
static void arp_timer_handler(struct k_timer *timer)
{
	k_work_submit(&arp.work);
}

static void arp_work_handler(struct k_work *work)
{
	if (arp.state == ARP_OFF || arp.note_count == 0) {
		return;
	}

	/* Turn off previous note */
	if (arp.last_note != 0) {
		send_midi_note(0, arp.last_note, 0, false);
	}

	uint8_t note;

	/* Handle different arpeggiator patterns */
	switch (arp.state) {
	case ARP_UP:
		note = arp.sorted_notes[arp.current_step];
		arp.current_step = (arp.current_step + 1) % arp.note_count;
		break;

	case ARP_DOWN:
		note = arp.sorted_notes[arp.note_count - 1 - arp.current_step];
		arp.current_step = (arp.current_step + 1) % arp.note_count;
		break;

	case ARP_UP_DOWN:
	case ARP_DOWN_UP:
		if (arp.ascending) {
			note = arp.sorted_notes[arp.current_step];
		} else {
			note = arp.sorted_notes[arp.note_count - 1 - arp.current_step];
		}

		/* Update step and direction */
		if (arp.ascending) {
			arp.current_step++;
			if (arp.current_step >= arp.note_count) {
				arp.current_step = arp.note_count > 1 ? arp.note_count - 2 : 0;
				arp.ascending = false;
			}
		} else {
			if (arp.current_step > 0) {
				arp.current_step--;
			} else {
				arp.current_step = 1;
				arp.ascending = true;
			}
		}
		break;

	case ARP_RANDOM:
		arp.current_step = sys_rand32_get() % arp.note_count;
		note = arp.sorted_notes[arp.current_step];
		break;

	default:
		return;
	}

	/* Send note on with current velocity */
	uint8_t velocity = (mode_values[MODE_VOLUME] * 127) / 100;
	send_midi_note(0, note, velocity, true);
	arp.last_note = note;
}

/* BPM Calculation */
static void update_arp_timer(void)
{
	if (arp.state == ARP_OFF) {
		k_timer_stop(&arp.timer);
		return;
	}

	/* Convert BPM to milliseconds (eighth notes for more musical timing) */
	uint32_t period_ms = 30000 / arp.bpm; /* 60000/2 = 30000 for eighth notes */
	k_timer_start(&arp.timer, K_MSEC(period_ms), K_MSEC(period_ms));
}

/* Check if a note is in the current scale */
static bool is_note_in_scale(uint8_t note)
{
	/* Adjust note based on root note to get scale position */
	uint8_t scale_position = note % 12;
	uint8_t root = mode_values[MODE_ROOT_NOTE];

	/* Calculate position relative to root note */
	scale_position = (12 + scale_position - root) % 12;

	return SCALES[mode_values[MODE_SCALE]][scale_position];
}

/* Map a key to the nearest note in scale */
static uint8_t map_to_scale(uint8_t note)
{
	/* If note is already in scale, return it */
	if (is_note_in_scale(note)) {
		return note;
	}

	/* Search up and down for nearest scale note */
	for (int offset = 1; offset < 12; offset++) {
		/* Try note above */
		if (note + offset <= 127 && is_note_in_scale(note + offset)) {
			return note + offset;
		}
		/* Try note below */
		if (note >= offset && is_note_in_scale(note - offset)) {
			return note - offset;
		}
	}

	return note; /* Fallback to original note if nothing found */
}

static void key_press(struct input_event *evt, void *user_data)
{
	/* Handle relative input events as parameter changes */
	if (evt->type == INPUT_EV_REL) {
		/* Update the current parameter value */
		mode_values[current_mode] += evt->value;

		/* Clamp values based on parameter type */
		switch (current_mode) {
		case MODE_VOLUME:
			if (mode_values[current_mode] < 0) {
				mode_values[current_mode] = 0;
			} else if (mode_values[current_mode] > 100) {
				mode_values[current_mode] = 100;
			}
			break;
		case MODE_OCTAVE:
			if (mode_values[current_mode] < -2) {
				mode_values[current_mode] = -2;
			} else if (mode_values[current_mode] > 8) {
				mode_values[current_mode] = 8;
			}
			break;
		case MODE_ARP_STATE:
			if (mode_values[current_mode] < 0) {
				mode_values[current_mode] = 0;
			} else if (mode_values[current_mode] >= ARP_STATE_COUNT) {
				mode_values[current_mode] = ARP_STATE_COUNT - 1;
			}
			arp.state = mode_values[current_mode];
			update_arp_timer();
			break;
		case MODE_ARP_BPM:
			if (mode_values[current_mode] < 20) {
				mode_values[current_mode] = 20;
			} else if (mode_values[current_mode] > 300) {
				mode_values[current_mode] = 300;
			}
			arp.bpm = mode_values[current_mode];
			update_arp_timer();
			break;
		case MODE_SCALE:
			if (mode_values[current_mode] < 0) {
				mode_values[current_mode] = 0;
			} else if (mode_values[current_mode] >= SCALE_COUNT) {
				mode_values[current_mode] = SCALE_COUNT - 1;
			}
			break;
		case MODE_ROOT_NOTE:
			if (mode_values[current_mode] < 0) {
				mode_values[current_mode] = 0;
			} else if (mode_values[current_mode] > 11) {
				mode_values[current_mode] = 11;
			}
			break;
		case MODE_REVERB:
			if (mode_values[current_mode] < 0) {
				mode_values[current_mode] = 0;
			} else if (mode_values[current_mode] > 127) {
				mode_values[current_mode] = 127;
			}
			/* Send MIDI CC for Reverb (CC #91) */
			send_midi_cc(0, 91, mode_values[current_mode]);
			break;
		case MODE_JOG:
			if (evt->value > 0) {
				/* Jog forward */
				send_midi_cc(0, 24, 1);
			} else if (evt->value < 0) {
				/* Jog backward */
				send_midi_cc(0, 24, 127);
			}
			/* Reset the value since we don't need to track it */
			mode_values[current_mode] = 0;
			break;
		}

		update_ui();
		return;
	}

	/* Handle key press for mode selection */
	if (evt->type == INPUT_EV_KEY && evt->code == INPUT_KEY_ENTER && evt->value) {
		current_mode = (current_mode + 1) % MODE_COUNT;
		update_ui();
		return;
	}

	/* Only handle other key presses */
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

	/* Calculate base note from key index and octave */
	uint8_t base_note = 60 + led_idx + (mode_values[MODE_OCTAVE] * 12);

	/* Map the note to the nearest note in the current scale */
	uint8_t note = map_to_scale(base_note);

	uint8_t velocity = (mode_values[MODE_VOLUME] * 127) / 100; /* Scale volume to velocity */

	/* Update LED strip */
	if (device_is_ready(led_strip)) {
		if (evt->value) {
			/* Key pressed - light up corresponding LED */
			if (note == base_note) {
				/* In-scale note: green */
				pixels[led_idx].r = 0;
				pixels[led_idx].g = velocity * 2;
				pixels[led_idx].b = 0;
			} else {
				/* Mapped note: yellow-green to indicate mapping */
				pixels[led_idx].r = velocity;
				pixels[led_idx].g = velocity * 2;
				pixels[led_idx].b = 0;
			}
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

	/* Handle note for arpeggiator */
	if (evt->value) {
		add_note(note);
		if (arp.state == ARP_OFF) {
			send_midi_note(0, note, velocity, true);
		}
	} else {
		remove_note(note);
		if (arp.state == ARP_OFF) {
			send_midi_note(0, note, 0, false);
		}
	}
}
INPUT_CALLBACK_DEFINE(NULL, key_press, NULL);

static void on_midi_packet(const struct device *dev, const struct midi_ump ump)
{
	printk("Received MIDI packet (MT=%X)\n", UMP_MT(ump));
	LOG_INF("Received MIDI packet (MT=%X)", UMP_MT(ump));

	/* Only handle MIDI1 channel voice messages */
	if (UMP_MT(ump) == UMP_MT_MIDI1_CHANNEL_VOICE) {
		uint8_t status = UMP_MIDI_STATUS(ump);
		uint8_t note = UMP_MIDI1_P1(ump);
		uint8_t velocity = UMP_MIDI1_P2(ump);
		uint8_t command = status & 0xF0;

		/* Handle Note On/Off messages */
		if (command == UMP_MIDI_NOTE_ON || command == UMP_MIDI_NOTE_OFF) {
			/* Calculate base note from current octave */
			uint8_t base_note = 60 + (mode_values[MODE_OCTAVE] * 12);

			/* Calculate LED index (0-11) based on note difference from base note */
			int8_t note_diff = note - base_note;
			if (note_diff >= 0 && note_diff < 12) {
				size_t led_idx = note_diff;

				/* Update LED strip */
				if (device_is_ready(led_strip)) {
					if (command == UMP_MIDI_NOTE_ON && velocity > 0) {
						/* Note On with velocity > 0 - light up LED */
						pixels[led_idx].r = 0;
						pixels[led_idx].g = velocity * 2;
						pixels[led_idx].b = 0;
					} else {
						/* Note Off or Note On with velocity 0 - turn off
						 * LED */
						pixels[led_idx].r = 0;
						pixels[led_idx].g = 0;
						pixels[led_idx].b = 0;
					}
					led_strip_update_rgb(led_strip, pixels, 12);
				}
			}
		}

		LOG_INF("Send back MIDI1 message %02X %02X %02X", status, note, velocity);
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

	/* Initialize arpeggiator */
	ring_buf_init(&arp.note_buf, sizeof(arp.note_buffer), arp.note_buffer);
	k_timer_init(&arp.timer, arp_timer_handler, NULL);
	k_work_init(&arp.work, arp_work_handler);
	arp.state = ARP_OFF;
	arp.bpm = mode_values[MODE_ARP_BPM];
	arp.current_step = 0;
	arp.last_note = 0;
	arp.ascending = true;
	arp.note_count = 0;

	/* Initialize display and LVGL */
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return -1;
	}

	lv_init();

	/* Create a simple label for our instrument UI */
	ui_label = lv_label_create(lv_scr_act());
	lv_obj_align(ui_label, LV_ALIGN_CENTER, 0, 0);
	update_ui();

	/* Turn off display blanking */
	display_blanking_off(display_dev);

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

	/* Main loop */
	while (1) {
		lv_timer_handler();
		k_sleep(K_MSEC(10));
	}

	return 0;
}
