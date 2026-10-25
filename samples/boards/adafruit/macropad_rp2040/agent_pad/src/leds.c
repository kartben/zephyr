/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(leds, LOG_LEVEL_INF);

#include "agent_pad.h"

#define NUM_PIXELS   12
#define FRAME_MS     33
#define FLASH_MS     120
#define LAYER_DIM    8

static const struct device *strip = DEVICE_DT_GET(DT_ALIAS(led_strip));

static struct led_rgb pixels[NUM_PIXELS];
static int64_t flash_until[NUM_PIXELS];
static int armed_key = -1;

/* Triangle wave, 0..255 over the given period. */
static uint8_t wave(int64_t t, uint32_t period_ms)
{
	uint32_t phase = (t % period_ms) * 512U / period_ms;

	return (phase < 256U) ? phase : 511U - phase;
}

static struct led_rgb scale(uint8_t r, uint8_t g, uint8_t b, uint8_t level)
{
	return (struct led_rgb){
		.r = (uint16_t)r * level / 255U,
		.g = (uint16_t)g * level / 255U,
		.b = (uint16_t)b * level / 255U,
	};
}

static struct led_rgb agent_pixel(const struct agent *agent, int64_t t)
{
	switch (agent->status) {
	case AGENT_IDLE:
		return scale(0xff, 0xff, 0xff, 12);
	case AGENT_THINKING:
		return scale(0x00, 0x60, 0xff, 20 + wave(t, 2000) / 4);
	case AGENT_DONE:
		return scale(0x00, 0xff, 0x20, 60);
	case AGENT_INPUT:
		return scale(0xff, 0x70, 0x00, 20 + wave(t, 800) / 3);
	case AGENT_ERROR:
		return scale(0xff, 0x00, 0x00, (t % 500) < 250 ? 80 : 10);
	default:
		return scale(0, 0, 0, 0);
	}
}

/* Soft white comet slowly sweeping the agent keys while nothing is connected. */
static struct led_rgb idle_pixel(int i, int64_t t)
{
	int head = (t / 400) % AGENT_SLOTS;
	int dist = (i - head + AGENT_SLOTS) % AGENT_SLOTS;

	return scale(0xff, 0xff, 0xff, dist == 0 ? 25 : (dist == 1 ? 8 : 0));
}

static void render(int64_t t)
{
	const struct layer *layer = &layers[state_layer()];
	bool empty = state_all_empty();
	struct agent agent;

	for (int i = 0; i < AGENT_SLOTS; i++) {
		if (empty) {
			pixels[i] = idle_pixel(i, t);
		} else {
			state_agent_get(i, &agent);
			pixels[i] = agent_pixel(&agent, t);
		}
	}

	for (int i = AGENT_SLOTS; i < NUM_PIXELS; i++) {
		pixels[i] = scale(layer->color[0], layer->color[1], layer->color[2], LAYER_DIM);
	}

	/* Pulse the key waiting for its confirming press. */
	if (armed_key >= 0) {
		pixels[armed_key] = scale(0xff, 0xff, 0xff, 40 + wave(t, 400) / 2);
	}

	for (int i = 0; i < NUM_PIXELS; i++) {
		if (t < flash_until[i]) {
			pixels[i] = scale(0xff, 0xff, 0xff, 100);
		}
	}
}

void leds_arm(int key)
{
	armed_key = (key >= 0 && key < NUM_PIXELS) ? key : -1;
}

void leds_flash(int key)
{
	if (key >= 0 && key < NUM_PIXELS) {
		flash_until[key] = k_uptime_get() + FLASH_MS;
	}
}

static void leds_thread(void *p1, void *p2, void *p3)
{
	while (true) {
		render(k_uptime_get());
		led_strip_update_rgb(strip, pixels, NUM_PIXELS);
		k_msleep(FRAME_MS);
	}
}

K_THREAD_DEFINE(leds_tid, 1024, leds_thread, NULL, NULL, NULL, 7, 0, K_TICKS_FOREVER);

int leds_init(void)
{
	if (!device_is_ready(strip)) {
		LOG_ERR("LED strip device not ready");
		return -EIO;
	}

	k_thread_start(leds_tid);

	return 0;
}
