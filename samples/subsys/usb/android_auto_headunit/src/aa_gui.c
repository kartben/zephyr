/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_gui.h"

#include <stdio.h>
#include <string.h>

#include <zephyr/display/cfb.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "aa_layout.h"
#include "aa_logo.h"
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
 * image, of which the protocol, TLS and the decoder leave a few thousand
 * bytes, so the panels are drawn here rather than by a GUI library. Text uses
 * the character framebuffer's own font, which the navigation display already
 * brings in.
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

#define RGB565(r, g, b) ((uint16_t)(((r) & 0xF8U) << 8 | ((g) & 0xFCU) << 3 | (b) >> 3))

/* Black, so that a panel on this display is the light it does not emit */
#define COLOUR_BACKDROP RGB565(0, 0, 0)
/* The artwork is flattened onto this, so the two have to stay in step */
#define COLOUR_PANEL    RGB565(0, 0, 0)
#define COLOUR_TRACK    RGB565(0, 0, 0)
#define COLOUR_GRID     RGB565(40, 48, 60)
#define COLOUR_TEXT     RGB565(200, 214, 230)

/* The logo has the first panel to itself */
#define LOGO_X (PANEL_X + (PANEL_W - AA_LOGO_WIDTH) / 2)
#define LOGO_Y (PANEL1_Y + (PANEL_H - AA_LOGO_HEIGHT) / 2)

/* The second panel names the threads on the left and charts them on the right */
#define INSET         16
#define TOP_THREADS   4
#define SWATCH_W      10
#define LEGEND_X      (PANEL_X + INSET)
#define LEGEND_TEXT_X (LEGEND_X + SWATCH_W + 4)
/* A share and ten characters of a font ten pixels wide, beside the swatch */
#define LEGEND_W      150
#define LEGEND_END    (LEGEND_X + LEGEND_W)
#define LEGEND_ROW    22
#define LEGEND_Y      (PANEL2_Y + (PANEL_H - TOP_THREADS * LEGEND_ROW) / 2)

#define PLOT_X (LEGEND_END + 12)
#define PLOT_Y (PANEL2_Y + INSET)
#define PLOT_W (PANEL_X + PANEL_W - INSET - PLOT_X)
#define PLOT_H (PANEL_H - 2 * INSET)

/* Threads followed at once, of which the busiest are named and drawn */
#define TRACKED   12
#define NAME_MAX  16
#define SAMPLE_MS 500

static const uint16_t series_colour[TOP_THREADS] = {
	RGB565(232, 96, 160), RGB565(240, 160, 64), RGB565(96, 200, 232), RGB565(128, 224, 128),
};

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

/* One thread's share of the processor, a sample per column of the chart */
struct thread_slot {
	k_tid_t tid;
	uint64_t cycles;
	char name[NAME_MAX];
	uint8_t percent[PLOT_W];
};

static struct thread_slot slots[TRACKED];
static uint64_t prev_all;
/* Slots the legend names, busiest first, or TRACKED for an empty row */
static uint8_t legend[TOP_THREADS];

static const struct cfb_font *font;

static void draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t colour)
{
	for (uint16_t row = 0U; row < h; row++) {
		uint16_t *out = &canvas[(size_t)(y + row) * GUI_W + x];

		for (uint16_t col = 0U; col < w; col++) {
			out[col] = colour;
		}
	}
}

/*
 * The character framebuffer registers its fonts in an iterable section, so the
 * smallest one can be drawn straight into the canvas without a framebuffer of
 * the kind that subsystem expects to be handed a display.
 */
static void font_pick(void)
{
	STRUCT_SECTION_FOREACH(cfb_font, candidate) {
		if ((candidate->caps & CFB_FONT_MONO_VPACKED) == 0) {
			continue;
		}
		if (font == NULL || candidate->height < font->height) {
			font = candidate;
		}
	}
}

static void draw_text(uint16_t x, uint16_t y, const char *text, uint16_t colour, uint16_t end)
{
	uint8_t rows = DIV_ROUND_UP(font->height, 8U);

	for (; *text != '\0' && (x + font->width) <= end; text++, x += font->width) {
		const uint8_t *glyph;

		if (*text < font->first_char || *text > font->last_char) {
			continue;
		}

		glyph = (const uint8_t *)font->data +
			(size_t)(*text - font->first_char) * font->width * rows;

		for (uint8_t gx = 0U; gx < font->width; gx++) {
			for (uint8_t gy = 0U; gy < font->height; gy++) {
				if ((glyph[gx * rows + gy / 8U] & BIT(gy % 8U)) != 0U) {
					canvas[(size_t)(y + gy) * GUI_W + x + gx] = colour;
				}
			}
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

void aa_gui_apply_argb8888(uint32_t *dst, uint16_t pitch, unsigned int idx)
{
	const uint16_t *src;
	struct aa_rect area;

	if (!ready || k_mutex_lock(&canvas_lock, K_NO_WAIT) != 0) {
		return;
	}

	if (next_area(idx, &area, &src)) {
		aa_scale_rgb565_argb8888(dst, pitch, &area, src, GUI_W);
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

/* The artwork, unpacked a run at a time straight into the canvas */
static void draw_logo(void)
{
	uint16_t *out = &canvas[(size_t)LOGO_Y * GUI_W + LOGO_X];
	uint32_t at = 0U;

	for (size_t i = 0U; i < ARRAY_SIZE(aa_logo_runs); i += 2U) {
		uint16_t colour = aa_logo_palette[aa_logo_runs[i + 1U]];

		for (uint8_t n = aa_logo_runs[i]; n > 0U; n--, at++) {
			out[(at / AA_LOGO_WIDTH) * GUI_W + (at % AA_LOGO_WIDTH)] = colour;
		}
	}

	mark_dirty(LOGO_X, LOGO_Y, AA_LOGO_WIDTH, AA_LOGO_HEIGHT);
}

/*
 * A slot per thread, kept for as long as the head unit runs. A thread that
 * ends leaves its history behind rather than handing its place to another,
 * which would graft one thread's samples onto another's line.
 */
static struct thread_slot *slot_for(k_tid_t tid)
{
	struct thread_slot *free_slot = NULL;

	for (unsigned int i = 0U; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].tid == tid) {
			return &slots[i];
		}
		if (slots[i].tid == NULL && free_slot == NULL) {
			free_slot = &slots[i];
		}
	}

	if (free_slot != NULL) {
		const char *name = k_thread_name_get(tid);

		free_slot->tid = tid;
		if (name != NULL && name[0] != '\0') {
			(void)strncpy(free_slot->name, name, NAME_MAX - 1U);
		} else {
			(void)snprintf(free_slot->name, NAME_MAX, "%p", (void *)tid);
		}
	}

	return free_slot;
}

/* An idle thread is the processor doing nothing, which the rest is measured against */
static bool is_idle(const struct thread_slot *slot)
{
	return strncmp(slot->name, "idle", 4) == 0;
}

static void thread_sample(const struct k_thread *cthread, void *user_data)
{
	k_tid_t tid = (k_tid_t)cthread;
	uint64_t window = *(uint64_t *)user_data;
	struct thread_slot *slot = slot_for(tid);
	k_thread_runtime_stats_t stats;
	uint64_t delta;

	if (slot == NULL || k_thread_runtime_stats_get(tid, &stats) != 0) {
		return;
	}

	delta = stats.execution_cycles - slot->cycles;
	slot->cycles = stats.execution_cycles;
	slot->percent[PLOT_W - 1U] =
		(window != 0U) ? (uint8_t)MIN(delta * 100U / window, 100U) : 0U;
}

/*
 * The busiest few, which are the ones the legend names and the chart draws.
 * Ranking on the newest sample alone made a thread that works in bursts drop
 * out of the four between one sample and the next, so its line came and went;
 * rank on the most each has taken over the history that is on screen, which
 * only changes when a thread has been quiet for the whole of it.
 */
static void rank_threads(void)
{
	uint8_t peak[TRACKED] = {0};

	for (uint8_t i = 0U; i < ARRAY_SIZE(slots); i++) {
		if (slots[i].tid == NULL || is_idle(&slots[i])) {
			continue;
		}
		for (uint16_t x = 0U; x < PLOT_W; x++) {
			peak[i] = MAX(peak[i], slots[i].percent[x]);
		}
	}

	for (unsigned int row = 0U; row < TOP_THREADS; row++) {
		uint8_t best = TRACKED;

		for (uint8_t i = 0U; i < ARRAY_SIZE(slots); i++) {
			bool taken = false;

			if (slots[i].tid == NULL || is_idle(&slots[i])) {
				continue;
			}
			for (unsigned int r = 0U; r < row; r++) {
				taken = taken || (legend[r] == i);
			}
			if (taken) {
				continue;
			}
			if (best == TRACKED || peak[i] > peak[best]) {
				best = i;
			}
		}

		legend[row] = best;
	}
}

static void sample_load(void)
{
	k_thread_runtime_stats_t all;
	uint64_t window;

	if (k_thread_runtime_stats_all_get(&all) != 0) {
		return;
	}

	/* Everything the processor did, idle included, since the last sample */
	window = all.execution_cycles - prev_all;
	prev_all = all.execution_cycles;

	for (unsigned int i = 0U; i < ARRAY_SIZE(slots); i++) {
		(void)memmove(slots[i].percent, &slots[i].percent[1], PLOT_W - 1U);
		slots[i].percent[PLOT_W - 1U] = 0U;
	}

	k_thread_foreach_unlocked(thread_sample, &window);
	rank_threads();
}

static uint16_t plot_top(uint8_t percent)
{
	return PLOT_Y + PLOT_H - (uint16_t)((uint32_t)percent * PLOT_H / 100U);
}

/*
 * One sample per column, with each column drawn from its own value to the
 * previous one so that a step between two samples is a line rather than two
 * disconnected marks.
 */
static void draw_series(const uint8_t *percent, uint16_t colour)
{
	uint16_t prev = plot_top(percent[0]);

	for (uint16_t x = 0U; x < PLOT_W; x++) {
		uint16_t top = plot_top(percent[x]);
		uint16_t y0 = MIN(prev, top);
		uint16_t y1 = MIN((uint16_t)(MAX(prev, top) + 1U),
				  (uint16_t)(PLOT_Y + PLOT_H - 1U));

		draw_rect(PLOT_X + x, y0, 1U, y1 - y0 + 1U, colour);
		prev = top;
	}
}

/* What the processor is doing, as a line per thread */
static void draw_chart(void)
{
	draw_rect(PLOT_X, PLOT_Y, PLOT_W, PLOT_H, COLOUR_TRACK);

	for (uint8_t at = 25U; at < 100U; at += 25U) {
		draw_rect(PLOT_X, plot_top(at), PLOT_W, 1U, COLOUR_GRID);
	}

	for (unsigned int row = 0U; row < TOP_THREADS; row++) {
		if (legend[row] < TRACKED) {
			draw_series(slots[legend[row]].percent, series_colour[row]);
		}
	}

	mark_dirty(PLOT_X, PLOT_Y, PLOT_W, PLOT_H);
}

static void legend_row(unsigned int row, uint16_t colour, uint8_t percent, const char *name)
{
	uint16_t y = LEGEND_Y + row * LEGEND_ROW;
	char text[NAME_MAX + 8];

	draw_rect(LEGEND_X, y + 3U, SWATCH_W, SWATCH_W, colour);
	(void)snprintf(text, sizeof(text), "%2u %s", percent, name);
	draw_text(LEGEND_TEXT_X, y, text, COLOUR_TEXT, LEGEND_END);
}

static void draw_legend(void)
{
	uint16_t w = LEGEND_END - LEGEND_X;

	draw_rect(LEGEND_X, LEGEND_Y, w, TOP_THREADS * LEGEND_ROW, COLOUR_PANEL);

	for (unsigned int row = 0U; row < TOP_THREADS; row++) {
		if (legend[row] >= TRACKED) {
			continue;
		}

		legend_row(row, series_colour[row], slots[legend[row]].percent[PLOT_W - 1U],
			   slots[legend[row]].name);
	}

	mark_dirty(LEGEND_X, LEGEND_Y, w, TOP_THREADS * LEGEND_ROW);
}

/*
 * The phone's pictures are what normally carries the GUI to the display, so
 * this only has to drive it when none are arriving. Animating the layout is
 * the one thing that cannot wait for a picture that may never come.
 */
static void gui_thread(void *p1, void *p2, void *p3)
{
	int64_t prev = k_uptime_get();
	int64_t next_sample = prev + SAMPLE_MS;

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
		 * half a second of a still chart is not something anyone sees.
		 */
		if (!moved && now >= next_sample) {
			next_sample = now + SAMPLE_MS;

			k_mutex_lock(&canvas_lock, K_FOREVER);
			sample_load();
			draw_chart();
			draw_legend();
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
	for (unsigned int i = 0U; i < ARRAY_SIZE(legend); i++) {
		legend[i] = TRACKED;
	}

	font_pick();
	if (font == NULL) {
		LOG_ERR("No font to label the chart with");
		return -ENOENT;
	}

	draw_rect(0U, 0U, GUI_W, GUI_H, COLOUR_BACKDROP);
	draw_rect(PANEL_X, PANEL1_Y, PANEL_W, PANEL_H, COLOUR_PANEL);
	draw_rect(PANEL_X, PANEL2_Y, PANEL_W, PANEL_H, COLOUR_PANEL);
	draw_logo();
	draw_chart();
	ready = true;

	k_thread_create(&gui_thread_data, gui_stack, K_THREAD_STACK_SIZEOF(gui_stack), gui_thread,
			NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_GUI_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&gui_thread_data, "aa_hu_gui");

	LOG_INF("GUI ready, %ux%u canvas at %p, %ux%u font", GUI_W, GUI_H, (void *)canvas,
		font->width, font->height);

	return 0;
}
