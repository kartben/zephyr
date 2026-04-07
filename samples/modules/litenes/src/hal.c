/*
 * Zephyr HAL implementation for LiteNES
 *
 * Copyright (c) 2024
 * SPDX-License-Identifier: Apache-2.0
 */

#include "hal.h"
#include "fce.h"
#include "nes.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>

#include <string.h>

LOG_MODULE_REGISTER(nes_hal, LOG_LEVEL_INF);

static const struct device *display_dev;
static uint16_t framebuffer[SCREEN_HEIGHT][SCREEN_WIDTH];
static uint16_t bg_color_rgb565;

/* Pre-computed NES palette in RGB565 format */
static uint16_t pal_rgb565[64];

/* Convert RGB888 to RGB565 */
static inline uint16_t rgb_to_565(int r, int g, int b)
{
	return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/* Input state — one bit per NES button */
static volatile uint32_t nes_buttons;

#define NES_BTN_POWER   BIT(0)
#define NES_BTN_A       BIT(1)
#define NES_BTN_B       BIT(2)
#define NES_BTN_SELECT  BIT(3)
#define NES_BTN_START   BIT(4)
#define NES_BTN_UP      BIT(5)
#define NES_BTN_DOWN    BIT(6)
#define NES_BTN_LEFT    BIT(7)
#define NES_BTN_RIGHT   BIT(8)

static void input_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	uint32_t mask = 0;

	switch (evt->code) {
	case INPUT_KEY_K:
		mask = NES_BTN_A;
		break;
	case INPUT_KEY_J:
		mask = NES_BTN_B;
		break;
	case INPUT_KEY_U:
		mask = NES_BTN_SELECT;
		break;
	case INPUT_KEY_I:
		mask = NES_BTN_START;
		break;
	case INPUT_KEY_W:
		mask = NES_BTN_UP;
		break;
	case INPUT_KEY_S:
		mask = NES_BTN_DOWN;
		break;
	case INPUT_KEY_A:
		mask = NES_BTN_LEFT;
		break;
	case INPUT_KEY_D:
		mask = NES_BTN_RIGHT;
		break;
	default:
		return;
	}

	if (evt->value) {
		nes_buttons |= mask;
	} else {
		nes_buttons &= ~mask;
	}
}

INPUT_CALLBACK_DEFINE(NULL, input_cb, NULL);

/* Frame timing */
void wait_for_frame(void)
{
	static int64_t next_frame_time;

	if (next_frame_time == 0) {
		next_frame_time = k_uptime_get();
	}

	next_frame_time += (1000 / FPS);
	int64_t now = k_uptime_get();
	int64_t sleep_ms = next_frame_time - now;

	if (sleep_ms > 0) {
		k_msleep(sleep_ms);
	} else {
		/* We're behind — reset timing to avoid perpetual catch-up */
		if (sleep_ms < -100) {
			next_frame_time = k_uptime_get();
		}
	}
}

void nes_hal_init(void)
{
	int i;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display device not ready");
		return;
	}

	/* Pre-compute palette in RGB565 */
	for (i = 0; i < 64; i++) {
		pal_rgb565[i] = rgb_to_565(palette[i].r, palette[i].g, palette[i].b);
	}

	/* Clear framebuffer */
	memset(framebuffer, 0, sizeof(framebuffer));

	/* Set pixel format to RGB565 */
	display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);

	display_blanking_off(display_dev);

	LOG_INF("NES HAL initialized (display: %dx%d)",
		SCREEN_WIDTH, SCREEN_HEIGHT);
}

void nes_set_bg_color(int c)
{
	int x, y;

	bg_color_rgb565 = pal_rgb565[c & 0x3F];

	/* Fill framebuffer with background color */
	for (y = 0; y < SCREEN_HEIGHT; y++) {
		for (x = 0; x < SCREEN_WIDTH; x++) {
			framebuffer[y][x] = bg_color_rgb565;
		}
	}
}

void nes_flush_buf(PixelBuf *buf)
{
	int i;

	for (i = 0; i < buf->size; i++) {
		Pixel *p = &buf->buf[i];
		int x = p->x;
		int y = p->y;

		if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
			framebuffer[y][x] = pal_rgb565[p->c & 0x3F];
		}
	}
}

void nes_flip_display(void)
{
	struct display_buffer_descriptor desc;
	int band_height = 16;
	int y;

	desc.width = SCREEN_WIDTH;
	desc.pitch = SCREEN_WIDTH;

	/* Write framebuffer to display in bands */
	for (y = 0; y < SCREEN_HEIGHT; y += band_height) {
		int h = band_height;

		if (y + h > SCREEN_HEIGHT) {
			h = SCREEN_HEIGHT - y;
		}

		desc.height = h;
		desc.buf_size = SCREEN_WIDTH * h * sizeof(uint16_t);
		display_write(display_dev, 0, y, &desc, &framebuffer[y][0]);
	}
}

int nes_key_state(int b)
{
	switch (b) {
	case 0: /* Power — always on */
		return 1;
	case 1:
		return !!(nes_buttons & NES_BTN_A);
	case 2:
		return !!(nes_buttons & NES_BTN_B);
	case 3:
		return !!(nes_buttons & NES_BTN_SELECT);
	case 4:
		return !!(nes_buttons & NES_BTN_START);
	case 5:
		return !!(nes_buttons & NES_BTN_UP);
	case 6:
		return !!(nes_buttons & NES_BTN_DOWN);
	case 7:
		return !!(nes_buttons & NES_BTN_LEFT);
	case 8:
		return !!(nes_buttons & NES_BTN_RIGHT);
	default:
		return 0;
	}
}
