/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_gui.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "aa_layout.h"
#include "aa_scale.h"
#include "aa_screen.h"

LOG_MODULE_REGISTER(aa_gui, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * The head unit's own GUI, which shares the display with the phone in split
 * screen mode. It is drawn into an RGB565 canvas of its own rather than onto
 * the display, and the screen composes that canvas with the phone's picture:
 * the display buffers are packed YUV for the controller to convert, and the
 * phone's picture has to be scaled into them either way.
 *
 * The board this runs on has a little over half a megabyte for the whole
 * image, of which the protocol, TLS and the decoder leave a few kilobytes, so
 * the panels are rectangles this file fills rather than a GUI library's.
 */

#define GUI_W (CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH / 2)
#define GUI_H CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT

/* How long the phone has to have sent nothing before the GUI drives the display */
#define VIDEO_IDLE_MS CONFIG_SAMPLE_AA_HU_GUI_IDLE_MS

#define MARGIN   12
#define PANEL_W  (GUI_W - 2 * MARGIN)
#define PANEL_H  ((GUI_H - 3 * MARGIN) / 2)
#define PANEL_X  MARGIN
#define PANEL1_Y MARGIN
#define PANEL2_Y (2 * MARGIN + PANEL_H)
#define INSET    16

#define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8U) << 8 | ((g) & 0xFCU) << 3 | (b) >> 3))

#define COLOUR_BACKDROP RGB565(16, 20, 24)
#define COLOUR_PANEL    RGB565(30, 42, 56)
#define COLOUR_TRACK    RGB565(20, 28, 38)
#define COLOUR_MARK     RGB565(96, 176, 232)
#define COLOUR_BAR      RGB565(216, 128, 72)

/* The sweep is redrawn every update; the bars change slowly and need not be */
#define BARS         24
#define BAR_PERIOD_MS 250

/* The sweeping mark, inside the first panel */
#define SWEEP_X (PANEL_X + INSET)
#define SWEEP_Y (PANEL1_Y + PANEL_H / 2 - 12)
#define SWEEP_W (PANEL_W - 2 * INSET)
#define SWEEP_H 24
#define MARK_W  64

/* The bar chart, inside the second panel */
#define PLOT_X (PANEL_X + INSET)
#define PLOT_Y (PANEL2_Y + INSET)
#define PLOT_W (PANEL_W - 2 * INSET)
#define PLOT_H (PANEL_H - 2 * INSET)
#define BAR_W  (PLOT_W / BARS)

static uint16_t canvas[GUI_W * GUI_H]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_GUI_CANVAS_SECTION) __aligned(32);

/* Held by whoever is drawing into the canvas or copying out of it */
static K_MUTEX_DEFINE(canvas_lock);
/* What has been drawn that a display buffer has not been given yet */
static struct aa_rect pending[AA_SCREEN_BUFFERS];
/* Where the GUI was on the display when each buffer was last written */
static uint16_t drawn_x[AA_SCREEN_BUFFERS];
static bool ready;

static K_THREAD_STACK_DEFINE(gui_stack, CONFIG_SAMPLE_AA_HU_GUI_STACK_SIZE);
static struct k_thread gui_thread_data;

static void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
	for (uint16_t row = 0U; row < h; row++) {
		uint16_t *out = &canvas[(size_t)(y + row) * GUI_W + x];

		for (uint16_t col = 0U; col < w; col++) {
			out[col] = colour;
		}
	}
}

/* Smallest rectangle holding both, so one copy covers everything that changed */
static void pending_add(struct aa_rect *p, const struct aa_rect *a)
{
	uint16_t x2;
	uint16_t y2;

	if (p->w == 0U) {
		*p = *a;
		return;
	}

	x2 = MAX((uint16_t)(p->x + p->w), (uint16_t)(a->x + a->w));
	y2 = MAX((uint16_t)(p->y + p->h), (uint16_t)(a->y + a->h));
	p->x = MIN(p->x, a->x);
	p->y = MIN(p->y, a->y);
	p->w = x2 - p->x;
	p->h = y2 - p->y;
}

static void mark_dirty(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	struct aa_rect area = {x, y, w, h};

	for (unsigned int i = 0U; i < ARRAY_SIZE(pending); i++) {
		pending_add(&pending[i], &area);
	}
}

/*
 * Which part of the canvas belongs in this display buffer, in display
 * coordinates. The GUI is pushed in from the right edge, so the canvas column
 * at the left of the panel is always its first one and only its width changes.
 */
static bool next_area(unsigned int idx, struct aa_rect *out, const uint16_t **src)
{
	struct aa_rect gui;
	uint16_t x0;
	uint16_t x1;

	aa_layout_gui(&gui);
	if (gui.w == 0U) {
		return false;
	}

	if (drawn_x[idx] != gui.x) {
		/* The panel has moved; everything on screen has to be redrawn */
		drawn_x[idx] = gui.x;
		pending[idx].w = 0U;
		*out = gui;
		*src = canvas;
		return true;
	}

	if (pending[idx].w == 0U || pending[idx].x >= gui.w) {
		return false;
	}

	/* A packed YUV pair shares its chroma, so keep to even columns */
	x0 = pending[idx].x & ~1U;
	x1 = MIN(gui.w, (uint16_t)((pending[idx].x + pending[idx].w + 1U) & ~1U));
	out->x = gui.x + x0;
	out->y = pending[idx].y;
	out->w = x1 - x0;
	out->h = pending[idx].h;
	*src = &canvas[(size_t)pending[idx].y * GUI_W + x0];
	pending[idx].w = 0U;

	return true;
}

void aa_gui_apply_yuyv(uint8_t *dst, uint16_t pitch, unsigned int idx)
{
	const uint16_t *src;
	struct aa_rect area;

	if (!ready || k_mutex_lock(&canvas_lock, K_NO_WAIT) != 0) {
		return;
	}

	if (next_area(idx, &area, &src)) {
		aa_scale_rgb565_yuyv(dst, pitch, &area, src, GUI_W);
	}

	k_mutex_unlock(&canvas_lock);
}

void aa_gui_apply_rgb565(uint16_t *dst, uint16_t pitch, unsigned int idx)
{
	const uint16_t *src;
	struct aa_rect area;

	if (!ready || k_mutex_lock(&canvas_lock, K_NO_WAIT) != 0) {
		return;
	}

	if (next_area(idx, &area, &src)) {
		aa_scale_rgb565_copy(dst, pitch, &area, src, GUI_W);
	}

	k_mutex_unlock(&canvas_lock);
}

bool aa_gui_dirty(void)
{
	struct aa_rect gui;

	if (!ready) {
		return false;
	}

	aa_layout_gui(&gui);
	if (gui.w == 0U) {
		/* Nothing of the GUI is on the display, so nothing is owed one */
		return false;
	}

	for (unsigned int i = 0U; i < ARRAY_SIZE(pending); i++) {
		if (pending[i].w != 0U || drawn_x[i] != gui.x) {
			return true;
		}
	}

	return false;
}

/*
 * A mark that runs from one end of its track to the other and back. It is the
 * cheapest thing to redraw that still says at a glance that the head unit's
 * own side of the display is alive rather than a still picture.
 */
static void draw_sweep(uint32_t phase)
{
	uint32_t span = SWEEP_W - MARK_W;
	uint32_t at = phase % (2U * span);

	draw_rect(SWEEP_X, SWEEP_Y, SWEEP_W, SWEEP_H, COLOUR_TRACK);
	draw_rect(SWEEP_X + (uint16_t)MIN(at, 2U * span - at), SWEEP_Y, MARK_W, SWEEP_H,
		  COLOUR_MARK);
	mark_dirty(SWEEP_X, SWEEP_Y, SWEEP_W, SWEEP_H);
}

/*
 * A bar per sample, oldest to the left, standing for the load the head unit is
 * under. The values are a placeholder; what the chart is made of is not.
 */
static void draw_bars(const uint8_t *values)
{
	draw_rect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H, COLOUR_TRACK);

	for (uint16_t i = 0U; i < BARS; i++) {
		uint16_t h = (uint16_t)((uint32_t)values[i] * PLOT_H / 100U);

		if (h == 0U) {
			continue;
		}
		draw_rect(PLOT_X + i * BAR_W, PLOT_Y + PLOT_H - h, BAR_W - 2U, h, COLOUR_BAR);
	}

	mark_dirty(PLOT_X, PLOT_Y, PLOT_W, PLOT_H);
}

/*
 * The phone's pictures are what normally carries the GUI to the display, so
 * this only has to drive it when none are arriving. Animating the layout is
 * the one thing that cannot wait for a picture that may never come.
 */
static void gui_thread(void *p1, void *p2, void *p3)
{
	static uint8_t bars[BARS];
	int64_t prev = k_uptime_get();
	int64_t next_bar = prev;
	uint32_t phase = 0U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		int64_t now = k_uptime_get();
		bool moved = aa_layout_step((uint32_t)(now - prev));

		prev = now;

		/*
		 * Redrawing while the panel is sliding would race the copy out
		 * of the canvas that every display buffer is then doing, and
		 * four hundred milliseconds of still widgets is not something
		 * anyone sees.
		 */
		if (!moved) {
			k_mutex_lock(&canvas_lock, K_FOREVER);

			phase += 6U;
			draw_sweep(phase);

			if (now >= next_bar) {
				next_bar = now + BAR_PERIOD_MS;
				memmove(bars, &bars[1], sizeof(bars) - 1U);
				bars[BARS - 1U] =
					(uint8_t)(30U + (phase / 3U) % 60U);
				draw_bars(bars);
			}

			k_mutex_unlock(&canvas_lock);
		}

		if ((now - aa_screen_last_picture()) > VIDEO_IDLE_MS &&
		    (moved || aa_gui_dirty())) {
			aa_screen_show(NULL, 0U, 0U);
		}

		k_sleep(K_MSEC(CONFIG_SAMPLE_AA_HU_GUI_PERIOD_MS));
	}
}

int aa_gui_init(void)
{
	for (unsigned int i = 0U; i < ARRAY_SIZE(drawn_x); i++) {
		drawn_x[i] = UINT16_MAX;
	}

	draw_rect(0U, 0U, GUI_W, GUI_H, COLOUR_BACKDROP);
	draw_rect(PANEL_X, PANEL1_Y, PANEL_W, PANEL_H, COLOUR_PANEL);
	draw_rect(PANEL_X, PANEL2_Y, PANEL_W, PANEL_H, COLOUR_PANEL);
	ready = true;

	k_thread_create(&gui_thread_data, gui_stack, K_THREAD_STACK_SIZEOF(gui_stack), gui_thread,
			NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_GUI_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gui_thread_data, "aa_hu_gui");

	LOG_INF("GUI ready, %ux%u canvas at %p", GUI_W, GUI_H, (void *)canvas);

	return 0;
}
