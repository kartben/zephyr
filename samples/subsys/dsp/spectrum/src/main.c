/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/dsp/dsp.h>
#include <zephyr/kernel.h>
#include <zephyr/mpipe/base/mpipe_app_sink.h>
#include <zephyr/mpipe/base/mpipe_app_src.h>
#include <zephyr/mpipe/base/mpipe_queue.h>
#include <zephyr/mpipe/base/mpipe_tee.h>
#include <zephyr/mpipe/mpipe_bin.h>
#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_pipeline.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
#include "usb_mic.h"
#endif

#ifdef CONFIG_SAMPLE_SPECTRUM_BATTERY
#include <zephyr/drivers/fuel_gauge.h>
#endif
#ifdef CONFIG_SAMPLE_SPECTRUM_HEADER_KEY
#include <zephyr/input/input.h>
#endif

#ifdef CONFIG_SAMPLE_SPECTRUM_DMIC
#include <zephyr/audio/dmic.h>
#if !DT_NODE_EXISTS(DT_ALIAS(dmic0))
#error "DMIC capture requires a dmic0 devicetree alias"
#endif
#endif

/* Allocate a strip and frequency history, rather than a full-screen buffer. */
#define FB_WIDTH    DT_PROP_OR(DT_CHOSEN(zephyr_display), width, 320)
#define FB_HEIGHT   DT_PROP_OR(DT_CHOSEN(zephyr_display), height, 240)
#define TILE_ROWS   16
#define STRIP_COUNT 2
#define FFT_SIZE    1024
#define HOP_SIZE    512
#define BLOCK_SIZE  256
#define SAMPLE_RATE 16000
#define BAND_COUNT  48
#define PI_F        3.14159265358979323846f
#define BATTERY_POLL_MS 5000

/* One strip is drawn while the writer thread sends the other to the display. */
static uint16_t strips[STRIP_COUNT][FB_WIDTH * TILE_ROWS];
static uint16_t *framebuffer;
static uint8_t waterfall[FB_HEIGHT][BAND_COUNT];
static int width;
static int height;
static int scale;
static int margin;
static int header;
static int footer;
static int plot_width;
static int plot_top;
static int plot_bottom;
static int bar_height;
static int water_y;
static int water_rows;
static int tile_y;
static int tile_rows;
static int water_head;
static int water_filled;
static bool swapped_bytes;
static int battery_pct = -1;
static bool header_shown = true;
static atomic_t header_toggled;
static uint16_t palette[256];
static uint16_t band_x[BAND_COUNT + 1];
static int16_t bar_top[BAND_COUNT];
static int16_t peak_top[BAND_COUNT];
static float samples[FFT_SIZE];
static float window[FFT_SIZE];
static float fft_in[FFT_SIZE];
static float fft_out[FFT_SIZE];
static float levels[BAND_COUNT];
static float peaks[BAND_COUNT];
K_MUTEX_DEFINE(spectrum_lock);
K_SEM_DEFINE(frame_ready, 0, 1);
K_SEM_DEFINE(strip_free, STRIP_COUNT, STRIP_COUNT);

struct strip {
	const uint16_t *buf;
	struct display_buffer_descriptor desc;
	int y;
};

K_MSGQ_DEFINE(strip_queue, sizeof(struct strip), STRIP_COUNT, 4);
static const struct device *display_device;
static uint16_t first_bin[BAND_COUNT];
static uint16_t last_bin[BAND_COUNT];
static struct zdsp_rfft_fast_instance_f32 fft;
static unsigned int sample_pos;
static unsigned int total_samples;
static struct mpipe pipeline;
static struct mpipe_app_src pcm_src;
static struct mpipe_queue fft_queue;
static struct mpipe_app_sink fft_sink;
#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
static struct mpipe_tee pcm_tee;
static struct mpipe_app_sink usb_sink;
#endif

#ifdef CONFIG_SAMPLE_SPECTRUM_DMIC
K_MEM_SLAB_DEFINE_STATIC(dmic_slab, BLOCK_SIZE * sizeof(int16_t), 4, 4);
#endif

/* Five columns by seven rows, 0-9, A-Z, then '%'. */
static const uint8_t font[37][7] = {
	{14, 17, 19, 21, 25, 17, 14}, {4, 12, 4, 4, 4, 4, 14},
	{14, 17, 1, 2, 4, 8, 31}, {30, 1, 1, 14, 1, 1, 30},
	{2, 6, 10, 18, 31, 2, 2}, {31, 16, 16, 30, 1, 1, 30},
	{14, 16, 16, 30, 17, 17, 14}, {31, 1, 2, 4, 8, 8, 8},
	{14, 17, 17, 14, 17, 17, 14}, {14, 17, 17, 15, 1, 1, 14},
	{14, 17, 17, 31, 17, 17, 17}, {30, 17, 17, 30, 17, 17, 30},
	{14, 17, 16, 16, 16, 17, 14}, {30, 17, 17, 17, 17, 17, 30},
	{31, 16, 16, 30, 16, 16, 31}, {31, 16, 16, 30, 16, 16, 16},
	{14, 17, 16, 23, 17, 17, 14}, {17, 17, 17, 31, 17, 17, 17},
	{14, 4, 4, 4, 4, 4, 14}, {7, 2, 2, 2, 18, 18, 12},
	{17, 18, 20, 24, 20, 18, 17}, {16, 16, 16, 16, 16, 16, 31},
	{17, 27, 21, 21, 17, 17, 17}, {17, 25, 21, 19, 17, 17, 17},
	{14, 17, 17, 17, 17, 17, 14}, {30, 17, 17, 30, 16, 16, 16},
	{14, 17, 17, 17, 21, 18, 13}, {30, 17, 17, 30, 20, 18, 17},
	{15, 16, 16, 14, 1, 1, 30}, {31, 4, 4, 4, 4, 4, 4},
	{17, 17, 17, 17, 17, 17, 14}, {17, 17, 17, 17, 17, 10, 4},
	{17, 17, 17, 21, 21, 21, 10}, {17, 17, 10, 4, 10, 17, 17},
	{17, 17, 10, 4, 4, 4, 4}, {31, 1, 2, 4, 8, 16, 31},
	{24, 25, 2, 4, 8, 19, 3},
};

static uint16_t rgb(uint8_t r, uint8_t g, uint8_t b)
{
	uint16_t color = ((uint16_t)(r >> 3) << 11) | ((uint16_t)(g >> 2) << 5) | (b >> 3);

	return swapped_bytes ? (color << 8) | (color >> 8) : color;
}

static uint16_t heat(float value)
{
	struct color {
		uint8_t r, g, b;
	};
	static const struct color stops[] = {
		{12, 17, 38}, {76, 43, 146}, {35, 156, 206},
		{64, 229, 191}, {255, 202, 90}, {255, 245, 211},
	};
	float scaled = CLAMP(value, 0.0f, 1.0f) * (ARRAY_SIZE(stops) - 1);
	unsigned int index = MIN((unsigned int)scaled, ARRAY_SIZE(stops) - 2);
	float blend = scaled - index;

	return rgb(stops[index].r + blend * (stops[index + 1].r - stops[index].r),
		   stops[index].g + blend * (stops[index + 1].g - stops[index].g),
		   stops[index].b + blend * (stops[index + 1].b - stops[index].b));
}

static void rect(int x, int y, int w, int h, uint16_t color)
{
	for (int row = MAX(y, tile_y); row < MIN(y + h, tile_y + tile_rows); row++) {
		for (int col = MAX(x, 0); col < MIN(x + w, width); col++) {
			framebuffer[(row - tile_y) * FB_WIDTH + col] = color;
		}
	}
}

static void label(const char *string, int x, int y, uint16_t color)
{
	for (; *string != '\0'; string++, x += 6 * scale) {
		unsigned int ch = (unsigned char)*string;
		int index = ch >= '0' && ch <= '9' ? ch - '0' :
			    ch >= 'A' && ch <= 'Z' ? ch - 'A' + 10 :
			    ch == '%' ? 36 : -1;

		if (index < 0) {
			continue;
		}
		for (int row = 0; row < 7; row++) {
			for (int col = 0; col < 5; col++) {
				if (font[index][row] & BIT(4 - col)) {
					rect(x + col * scale, y + row * scale,
					     scale, scale, color);
				}
			}
		}
	}
}

/* Draw the battery icon and percentage right-aligned at x, return the left edge. */
static int draw_battery(int x, int y)
{
	const uint16_t outline = rgb(124, 146, 186);
	uint16_t level = battery_pct > 50 ? rgb(64, 229, 141) :
			 battery_pct > 20 ? rgb(255, 202, 90) : rgb(255, 92, 92);
	char text[5];
	int len;

	if (battery_pct < 0) {
		return x;
	}
	len = snprintk(text, sizeof(text), "%d%%", battery_pct);
	x -= 20 * scale;
	rect(x + 18 * scale, y + 2 * scale, 2 * scale, 5 * scale, outline);
	rect(x, y, 18 * scale, 9 * scale, outline);
	rect(x + scale, y + scale, 16 * scale, 7 * scale, rgb(17, 23, 47));
	rect(x + 2 * scale, y + 2 * scale, MAX(1, battery_pct * 14 / 100) * scale, 5 * scale,
	     level);
	x -= (4 + 6 * len) * scale;
	label(text, x, y + scale, rgb(214, 224, 250));

	return x;
}

static void draw_header(void)
{
	const uint16_t text = rgb(214, 224, 250);
	const uint16_t dim = rgb(124, 146, 186);
	const char *mode = IS_ENABLED(CONFIG_SAMPLE_SPECTRUM_DMIC) ? "MIC" : "DEMO";
	int right;

	rect(0, 0, width, header, rgb(17, 23, 47));
	rect(margin, 9 * scale, 3 * scale, 13 * scale, rgb(174, 141, 255));
	right = draw_battery(width - margin, 11 * scale);
	/* Narrow panels have no room for both the title and the battery. */
	if (right >= margin + 58 * scale) {
		label("SPECTRUM", margin + 9 * scale, 12 * scale, text);
	}
	right -= (6 * strlen(mode) + (right < width - margin ? 6 : 0)) * scale;
	if (right >= margin + 63 * scale) {
		label(mode, right, 12 * scale, dim);
	}
	rect(0, header - scale, width, scale, rgb(48, 57, 88));
}

static void init_graphics(void)
{
	const uint16_t text = rgb(214, 224, 250);
	const uint16_t dim = rgb(124, 146, 186);

	rect(0, tile_y, width, tile_rows, rgb(10, 14, 32));
	if (header_shown) {
		draw_header();
	}
	rect(0, water_y - 20 * scale, width, scale, rgb(48, 57, 88));
	rect(0, water_y - 19 * scale, width, 19 * scale, rgb(17, 23, 47));
	label("WATERFALL", margin, water_y - 14 * scale, text);
	if (width >= 220 * scale) {
		label("TIME", width - 38 * scale, water_y - 14 * scale, dim);
	}
	rect(0, height - footer, width, footer, rgb(17, 23, 47));
	label("100", margin, height - 10 * scale, dim);
	if (width >= 120 * scale) {
		label("500", width * 43 / 100, height - 10 * scale, dim);
		label("1K", width * 58 / 100, height - 10 * scale, dim);
	}
	label("5K", width - 27 * scale, height - 10 * scale, dim);
}

static void init_fft(void)
{
	for (int i = 0; i < FFT_SIZE; i++) {
		window[i] = 0.5f - 0.5f * cosf(2.0f * PI_F * i / (FFT_SIZE - 1));
	}
	for (int i = 0; i < BAND_COUNT; i++) {
		float low = 50.0f * powf(160.0f, (float)i / BAND_COUNT);
		float high = 50.0f * powf(160.0f, (float)(i + 1) / BAND_COUNT);

		first_bin[i] = MAX(1, (int)(low * FFT_SIZE / SAMPLE_RATE));
		last_bin[i] = MIN(FFT_SIZE / 2, MAX(first_bin[i],
						       (int)(high * FFT_SIZE / SAMPLE_RATE)));
	}
}

static void analyze(void)
{
	for (int i = 0; i < FFT_SIZE; i++) {
		fft_in[i] = samples[(sample_pos + i) % FFT_SIZE] * window[i];
	}
	zdsp_rfft_fast_f32(&fft, fft_in, fft_out, 0);

	k_mutex_lock(&spectrum_lock, K_FOREVER);
	for (int band = 0; band < BAND_COUNT; band++) {
		float magnitude = 0.0f;

		for (int bin = first_bin[band]; bin <= last_bin[band]; bin++) {
			/* The fast RFFT packs Nyquist in slot 1, not slot 1024. */
			float re = bin == FFT_SIZE / 2 ? fft_out[1] : fft_out[2 * bin];
			float im = bin == FFT_SIZE / 2 ? 0.0f : fft_out[2 * bin + 1];

			magnitude = MAX(magnitude, sqrtf(re * re + im * im));
		}
		float db = 20.0f * log10f(magnitude * (4.0f / FFT_SIZE) + 1.0e-6f);
		float target = CLAMP((db + 75.0f) / 70.0f, 0.0f, 1.0f);
		float attack = target > levels[band] ? 0.65f : 0.16f;

		levels[band] += attack * (target - levels[band]);
		peaks[band] = MAX(levels[band], peaks[band] - 0.018f);
	}
	k_mutex_unlock(&spectrum_lock);
}

static void fill(uint16_t *line, int x0, int x1, uint16_t color)
{
	for (int x = x0; x < x1; x++) {
		line[x] = color;
	}
}

/* Fill one row of the bar plot or waterfall; the rest of the screen is static. */
static void draw_row(uint16_t *line, int y)
{
	const uint16_t background = rgb(10, 14, 32);
	int grid_start = header + 17 * scale;
	bool grid_row = y >= grid_start && y < plot_bottom &&
			(y - grid_start) % (16 * scale) < scale;
	uint16_t empty = grid_row ? rgb(28, 38, 65) : background;
	uint16_t bar = palette[MIN(255, (plot_bottom - y) * 255 / bar_height + 25)];
	uint16_t peak = rgb(229, 214, 255);
	uint16_t *plot = &line[margin];

	fill(line, 0, margin, background);
	fill(line, margin + plot_width, width, background);
	if (y >= water_y) {
		const uint8_t *row = waterfall[(water_head + y - water_y) % water_rows];

		for (int i = 0; i < BAND_COUNT; i++) {
			fill(plot, band_x[i], band_x[i + 1], palette[row[i]]);
		}
		return;
	}
	for (int i = 0; i < BAND_COUNT; i++) {
		int bar_end = MAX(band_x[i] + 1, band_x[i + 1] - scale);
		uint16_t color = empty;

		if (y >= peak_top[i] && y < peak_top[i] + scale) {
			color = peak;
		} else if (y >= bar_top[i] && y < plot_bottom) {
			color = bar;
		}
		fill(plot, band_x[i], bar_end, color);
		fill(plot, bar_end, band_x[i + 1], empty);
	}
}

static void draw_rows(int y0, int y1, bool background, bool last)
{
	static unsigned int next;

	for (tile_y = y0; tile_y < y1; tile_y += TILE_ROWS) {
		tile_rows = MIN(TILE_ROWS, y1 - tile_y);
		(void)k_sem_take(&strip_free, K_FOREVER);
		framebuffer = strips[next];
		next = (next + 1) % STRIP_COUNT;

		if (background) {
			init_graphics();
		}
		for (int y = tile_y; y < tile_y + tile_rows; y++) {
			if ((y >= plot_top && y < plot_bottom + scale) ||
			    (y >= water_y && y < water_y + water_filled)) {
				draw_row(&framebuffer[(y - tile_y) * FB_WIDTH], y);
			}
		}

		struct strip strip = {
			.buf = framebuffer,
			.desc = {
				.buf_size = tile_rows * FB_WIDTH * sizeof(framebuffer[0]),
				.width = width,
				.height = tile_rows,
				.pitch = FB_WIDTH,
				.frame_incomplete = !last || tile_y + tile_rows < y1,
			},
			.y = tile_y,
		};

		(void)k_msgq_put(&strip_queue, &strip, K_FOREVER);
	}
}

#ifdef CONFIG_SAMPLE_SPECTRUM_BATTERY
/* Return true when the displayed percentage changed. */
static bool poll_battery(void)
{
	static const struct device *const gauge = DEVICE_DT_GET(DT_ALIAS(fuel_gauge0));
	static int64_t next_poll;
	union fuel_gauge_prop_val val;
	int pct;

	if (k_uptime_get() < next_poll) {
		return false;
	}
	next_poll = k_uptime_get() + BATTERY_POLL_MS;
	if (!device_is_ready(gauge) ||
	    fuel_gauge_get_prop(gauge, FUEL_GAUGE_RELATIVE_STATE_OF_CHARGE_PCT, &val) < 0) {
		pct = -1;
	} else {
		pct = MIN(val.relative_state_of_charge_pct, 100);
	}
	if (pct == battery_pct) {
		return false;
	}
	battery_pct = pct;

	return true;
}
#else
static bool poll_battery(void)
{
	return false;
}
#endif

#ifdef CONFIG_SAMPLE_SPECTRUM_HEADER_KEY
/* The first user button hides the header and lets the plots take its space. */
static void header_key_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->type == INPUT_EV_KEY && evt->code == INPUT_KEY_0 && evt->value == 1) {
		atomic_set(&header_toggled, 1);
	}
}

INPUT_CALLBACK_DEFINE(NULL, header_key_cb, NULL);
#endif

static void layout(void)
{
	header = header_shown ? 31 * scale : 0;
	plot_top = header + 3 * scale;
	plot_bottom = header + 2 * (height - header - footer) / 5;
	bar_height = plot_bottom - (header + 9 * scale);
	water_y = plot_bottom + 28 * scale;
	water_rows = height - footer - 5 * scale - water_y;
	water_head = 0;
	water_filled = 0;
}

static void render(void)
{
	static bool background_drawn;
	bool battery_changed = poll_battery();

	if (atomic_clear(&header_toggled) != 0) {
		header_shown = !header_shown;
		layout();
		background_drawn = false;
	}

	k_mutex_lock(&spectrum_lock, K_FOREVER);
	water_head = (water_head + water_rows - 1) % water_rows;
	water_filled = MIN(water_filled + 1, water_rows);
	for (int i = 0; i < BAND_COUNT; i++) {
		waterfall[water_head][i] = (uint8_t)(levels[i] * 255.0f);
		bar_top[i] = plot_bottom - (int)(levels[i] * bar_height);
		peak_top[i] = plot_bottom - (int)(peaks[i] * bar_height);
	}
	k_mutex_unlock(&spectrum_lock);

	if (!background_drawn) {
		draw_rows(0, height, true, true);
		background_drawn = true;
	} else {
		if (battery_changed && header_shown) {
			draw_rows(0, header, true, false);
		}
		draw_rows(plot_top, plot_bottom + scale, false, false);
		draw_rows(water_y, water_y + water_filled, false, true);
	}
}

static void display_thread(void *a, void *b, void *c)
{
	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		k_sem_take(&frame_ready, K_FOREVER);
		render();
	}
}

K_THREAD_DEFINE(display_thread_id, 4096, display_thread, NULL, NULL, NULL, 8, 0, 0);

static void writer_thread(void *a, void *b, void *c)
{
	unsigned int failures = 0;

	ARG_UNUSED(a);
	ARG_UNUSED(b);
	ARG_UNUSED(c);

	while (true) {
		struct strip strip;

		(void)k_msgq_get(&strip_queue, &strip, K_FOREVER);

		int err = display_write(display_device, 0, strip.y, &strip.desc, strip.buf);

		k_sem_give(&strip_free);
		if (err < 0 && failures++ % 60 == 0) {
			printk("display_write failed: %d\n", err);
		}
	}
}

/* Higher priority than the renderer, so a queued strip is sent right away. */
K_THREAD_DEFINE(writer_thread_id, 2048, writer_thread, NULL, NULL, NULL, 7, 0, 0);

static void add_samples(const int16_t *pcm, size_t count)
{
	for (size_t i = 0; i < count; i++) {
		samples[sample_pos] = pcm[i] / 32768.0f;
		sample_pos = (sample_pos + 1) % FFT_SIZE;
		total_samples++;
		if (total_samples >= FFT_SIZE &&
		    (total_samples - FFT_SIZE) % HOP_SIZE == 0) {
			analyze();
			/* A frame still pending when the next one arrives is dropped. */
			k_sem_give(&frame_ready);
		}
	}
}

static void fft_pcm_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);
	add_samples((const int16_t *)buf->data,
		    mpipe_buffer_get_meta(buf)->bytes_used / sizeof(int16_t));
}

#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
static void usb_pcm_cb(const struct net_buf *buf, void *user_data)
{
	ARG_UNUSED(user_data);
	usb_mic_feed((const int16_t *)buf->data,
		     mpipe_buffer_get_meta(buf)->bytes_used / sizeof(int16_t));
}
#endif

static int init_pcm_pipeline(void)
{
	struct mpipe_structure caps;
	const struct mpipe_app_sink_cb fft_cb = {.fn = fft_pcm_cb};
	const enum mpipe_base_queue_leak leak = MPIPE_BASE_QUEUE_LEAK_OLDEST;
	int err;

	err = mpipe_structure_init_fields(&caps, MPIPE_MEDIA_AUDIO_PCM,
		MPIPE_CAPS_SAMPLE_RATE, MPIPE_TYPE_UINT, SAMPLE_RATE,
		MPIPE_CAPS_BITWIDTH, MPIPE_TYPE_UINT, 16,
		MPIPE_CAPS_NUM_OF_CHANNEL, MPIPE_TYPE_UINT, 1,
		MPIPE_CAPS_END);
	if (err < 0) {
		return err;
	}
	err = mpipe_pipeline_init(&pipeline, 1);
	err = err ?: mpipe_app_src_init(&pcm_src, 2);
	err = err ?: mpipe_queue_init(&fft_queue, 3);
	err = err ?: mpipe_app_sink_init(&fft_sink, 4);
#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
	err = err ?: mpipe_tee_init(&pcm_tee, 5);
	err = err ?: mpipe_app_sink_init(&usb_sink, 6);
#endif
	if (err < 0) {
		return err;
	}
	err = mpipe_object_set_properties((struct mpipe_object *)&pcm_src,
		MPIPE_PROP_BASE_APP_SRC_CAPS, &caps, MPIPE_PROP_LIST_END);
	err = err ?: mpipe_object_set_properties((struct mpipe_object *)&fft_sink,
		MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
		MPIPE_PROP_BASE_APP_SINK_CB, &fft_cb, MPIPE_PROP_LIST_END);
	err = err ?: mpipe_object_set_properties((struct mpipe_object *)&fft_queue,
		MPIPE_PROP_BASE_QUEUE_LEAK, &leak, MPIPE_PROP_LIST_END);
#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
	const struct mpipe_app_sink_cb usb_cb = {.fn = usb_pcm_cb};

	err = err ?: mpipe_object_set_properties((struct mpipe_object *)&usb_sink,
		MPIPE_PROP_BASE_APP_SINK_CAPS, &caps,
		MPIPE_PROP_BASE_APP_SINK_CB, &usb_cb, MPIPE_PROP_LIST_END);
	err = err ?: mpipe_bin_add((struct mpipe_bin *)&pipeline,
		(struct mpipe_element *)&pcm_src, (struct mpipe_element *)&pcm_tee,
		(struct mpipe_element *)&fft_queue, (struct mpipe_element *)&fft_sink,
		(struct mpipe_element *)&usb_sink, NULL);
	err = err ?: mpipe_element_link((struct mpipe_element *)&pcm_src,
		(struct mpipe_element *)&pcm_tee, NULL);
	err = err ?: mpipe_element_link((struct mpipe_element *)&pcm_tee,
		(struct mpipe_element *)&fft_queue, (struct mpipe_element *)&fft_sink, NULL);
	if (err == 0) {
		mpipe_pad_link(&pcm_tee.src_pads[1], &usb_sink.sink.sink_pad);
	}
#else
	err = err ?: mpipe_bin_add((struct mpipe_bin *)&pipeline,
		(struct mpipe_element *)&pcm_src, (struct mpipe_element *)&fft_queue,
		(struct mpipe_element *)&fft_sink, NULL);
	err = err ?: mpipe_element_link((struct mpipe_element *)&pcm_src,
		(struct mpipe_element *)&fft_queue, (struct mpipe_element *)&fft_sink, NULL);
#endif
	return err ?: mpipe_element_set_state((struct mpipe_element *)&pipeline,
					     MPIPE_STATE_PLAYING);
}

static void push_samples(const int16_t *pcm, size_t count)
{
	int err = mpipe_app_src_push(&pcm_src, pcm, count * sizeof(*pcm), K_NO_WAIT);

	if (err < 0) {
		/* Capture must continue if the display branch falls behind. */
		static unsigned int dropped;

		if ((dropped++ & 63U) == 0U) {
			printk("PCM pipeline dropped a block: %d\n", err);
		}
	}
}

#ifdef CONFIG_SAMPLE_SPECTRUM_DMIC
static void capture(void)
{
	const struct device *dmic = DEVICE_DT_GET(DT_ALIAS(dmic0));
	struct pcm_stream_cfg stream = {
		.pcm_rate = SAMPLE_RATE,
		.pcm_width = 16,
		.block_size = BLOCK_SIZE * sizeof(int16_t),
		.mem_slab = &dmic_slab,
	};
	struct dmic_cfg config = {
		.io = {
			.min_pdm_clk_freq = 1000000,
			.max_pdm_clk_freq = 3500000,
			.min_pdm_clk_dc = 40,
			.max_pdm_clk_dc = 60,
		},
		.streams = &stream,
		.channel = {
			.req_num_streams = 1,
			.req_num_chan = 1,
			.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
		},
	};

	if (!device_is_ready(dmic) || dmic_configure(dmic, &config) < 0) {
		printk("DMIC not ready or configuration unsupported\n");
		return;
	}
	int err = dmic_trigger(dmic, DMIC_TRIGGER_START);

	if (err < 0) {
		printk("DMIC start failed: %d\n", err);
		return;
	}
	while (true) {
		void *block;
		size_t size;

		err = dmic_read(dmic, 0, &block, &size, 1000);
		if (err < 0) {
			printk("DMIC read failed: %d\n", err);
			break;
		}
		push_samples(block, size / sizeof(int16_t));
		k_mem_slab_free(&dmic_slab, block);
	}
	(void)dmic_trigger(dmic, DMIC_TRIGGER_STOP);
}
#else
static void generate(void)
{
	static int16_t block[BLOCK_SIZE];
	unsigned int n = 0;
	int64_t next_block = k_uptime_get();

	while (true) {
		for (int i = 0; i < BLOCK_SIZE; i++, n++) {
			float t = (float)n / SAMPLE_RATE;
			float pulse = 0.5f + 0.5f * sinf(2.0f * PI_F * 1.7f * t);
			float value = 0.45f * sinf(2.0f * PI_F * 220.0f * t) +
				      0.25f * sinf(2.0f * PI_F * 710.0f * t) +
				      pulse * 0.22f * sinf(2.0f * PI_F * 2100.0f * t);

			block[i] = (int16_t)(value * 32767.0f);
		}
		push_samples(block, ARRAY_SIZE(block));
		next_block += 1000 * BLOCK_SIZE / SAMPLE_RATE;
		int64_t delay = next_block - k_uptime_get();

		if (delay > 0) {
			k_sleep(K_MSEC(delay));
		} else if (delay < -32) {
			/* Do not race through old audio after a long scheduling delay. */
			next_block = k_uptime_get();
		}
	}
}
#endif

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct display_capabilities caps;
	int err;

	if (!device_is_ready(display)) {
		printk("Display not ready\n");
		return 0;
	}
	display_get_capabilities(display, &caps);
	width = MIN(caps.x_resolution, FB_WIDTH);
	height = MIN(caps.y_resolution, FB_HEIGHT);
	if (width < 80 || height < 160 ||
	    !(caps.supported_pixel_formats & (PIXEL_FORMAT_RGB_565 |
					      PIXEL_FORMAT_RGB_565X))) {
		printk("Need an 80x160 RGB565 or RGB565X display\n");
		return 0;
	}
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565 &&
	    caps.current_pixel_format != PIXEL_FORMAT_RGB_565X) {
		err = display_set_pixel_format(display,
			(caps.supported_pixel_formats & PIXEL_FORMAT_RGB_565) ?
			PIXEL_FORMAT_RGB_565 : PIXEL_FORMAT_RGB_565X);
		if (err < 0) {
			printk("RGB565 selection failed: %d\n", err);
			return 0;
		}
		display_get_capabilities(display, &caps);
	}
	swapped_bytes = caps.current_pixel_format == PIXEL_FORMAT_RGB_565X;
	scale = MAX(1, MIN(width / 320, height / 240));
	margin = 10 * scale;
	footer = 17 * scale;
	plot_width = width - 2 * margin;
	layout();
	for (int i = 0; i < ARRAY_SIZE(palette); i++) {
		palette[i] = heat(i / 255.0f);
	}
	for (int i = 0; i <= BAND_COUNT; i++) {
		band_x[i] = i * plot_width / BAND_COUNT;
	}
	if (zdsp_rfft_fast_init_f32(&fft, FFT_SIZE) != ZDSP_TRANSFORM_STATUS_OK) {
		printk("zDSP FFT initialization failed\n");
		return 0;
	}
	init_fft();
	err = display_blanking_off(display);
	if (err < 0 && err != -ENOSYS) {
		printk("Display blanking failed: %d\n", err);
		return 0;
	}
	display_device = display;
#ifdef CONFIG_SAMPLE_SPECTRUM_USB_MIC
	err = usb_mic_init();
	if (err < 0) {
		printk("USB microphone initialization failed: %d\n", err);
		return 0;
	}
#endif
	err = init_pcm_pipeline();
	if (err < 0) {
		printk("PCM pipeline initialization failed: %d\n", err);
		return 0;
	}
	printk("zDSP audio spectrum ready (%s)\n",
	       IS_ENABLED(CONFIG_SAMPLE_SPECTRUM_DMIC) ? "DMIC" : "generated signal");
#ifdef CONFIG_SAMPLE_SPECTRUM_DMIC
	capture();
#else
	generate();
#endif
	return 0;
}
