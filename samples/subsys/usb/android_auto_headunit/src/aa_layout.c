/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_layout.h"

#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(aa_layout, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Where the phone's picture and the head unit's own GUI sit on the display.
 * The two share the width: the picture keeps the left of it and the GUI takes
 * what is left over, so moving one edge moves both and the GUI appears to be
 * pushed in from the right as the picture shrinks.
 */



/* Fixed point one, for the fraction of the way to split screen */
#define PROGRESS_ONE 1000U

#ifdef CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN

/* Where the animation is heading, as PROGRESS_ONE or zero */
static atomic_t target;
/* Where it has got to. Only the thread calling aa_layout_step() writes it. */
static uint32_t progress;

/*
 * Ease the movement in and out rather than running it at a constant speed,
 * which reads as the panel being dragged rather than snapped into place.
 * 3t^2 - 2t^3 in fixed point; the widest intermediate is 2 * 1000^3, which
 * still fits an unsigned 32 bit product.
 */
static uint32_t ease(uint32_t p)
{
	return (3U * p * p - 2U * p * p * p / PROGRESS_ONE) / PROGRESS_ONE;
}

/* How far the video edges have travelled towards the split screen position */
static uint16_t video_width(void)
{
	uint16_t w = SURFACE_W - (uint16_t)((SURFACE_W / 2U) * ease(progress) / PROGRESS_ONE);

	/* Packed YUV 4:2:2 carries one chroma pair per two pixels */
	return w & ~1U;
}

static uint16_t video_height(void)
{
	uint16_t h = SURFACE_H - (uint16_t)((SURFACE_H / 2U) * ease(progress) / PROGRESS_ONE);

	return h & ~1U;
}

bool aa_layout_gui_visible(void)
{
	return video_width() < SURFACE_W;
}

bool aa_layout_moving(void)
{
	return progress != (uint32_t)atomic_get(&target);
}

void aa_layout_toggle(void)
{
	uint32_t next = (atomic_get(&target) == 0) ? PROGRESS_ONE : 0U;

	atomic_set(&target, (atomic_val_t)next);
	LOG_INF("Display mode: %s", (next != 0U) ? "split screen" : "full screen");
}

bool aa_layout_step(uint32_t elapsed_ms)
{
	uint32_t want = (uint32_t)atomic_get(&target);
	uint32_t by;

	if (progress == want) {
		return false;
	}

	by = MAX(elapsed_ms * PROGRESS_ONE / CONFIG_SAMPLE_AA_HU_SPLIT_ANIM_MS, 1U);
	if (want > progress) {
		progress = MIN(progress + by, want);
	} else {
		progress = (by > progress) ? 0U : (progress - by);
	}

	return true;
}

#ifdef CONFIG_SAMPLE_AA_HU_SPLIT_BUTTON
#define SPLIT_BUTTON DT_ALIAS(aa_longpress)

/*
 * The button reports a code of its own once it has been held long enough, and
 * a different one when it is let go before that, so the short press is free
 * for something else.
 */
static void split_button_cb(struct input_event *evt, void *user_data)
{
	ARG_UNUSED(user_data);

	if (evt->code != DT_PROP_BY_IDX(SPLIT_BUTTON, long_codes, 0) || evt->value == 0) {
		return;
	}

	aa_layout_toggle();
}

INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(SPLIT_BUTTON), split_button_cb, NULL);
#endif /* CONFIG_SAMPLE_AA_HU_SPLIT_BUTTON */

#else /* CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN */

static uint16_t video_width(void)
{
	return SURFACE_W;
}

static uint16_t video_height(void)
{
	return SURFACE_H;
}

bool aa_layout_gui_visible(void)
{
	return false;
}

bool aa_layout_moving(void)
{
	return false;
}

void aa_layout_toggle(void)
{
}

bool aa_layout_step(uint32_t elapsed_ms)
{
	ARG_UNUSED(elapsed_ms);

	return false;
}

#endif /* CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN */

void aa_layout_area(struct aa_rect *r)
{
	r->x = 0U;
	r->y = 0U;
	r->w = video_width();
	r->h = video_height();
}

void aa_layout_video(struct aa_rect *r)
{
	uint16_t aw = video_width();
	uint16_t ah = video_height();
	/*
	 * Never larger than the picture the phone sends. Scaling it up costs
	 * more of the frame than the decode does and the panel is not the
	 * shape of the stream in any case, so it sits in the middle of what it
	 * owns and the rest is left dark.
	 */
	uint16_t w = MIN(aw, (uint16_t)STREAM_W) & ~1U;
	uint16_t h = MIN(ah, (uint16_t)STREAM_H) & ~1U;

	r->w = w;
	r->h = h;
	r->x = (uint16_t)((aw - w) / 2U) & ~1U;
	r->y = (uint16_t)((ah - h) / 2U);
}

void aa_layout_gui(struct aa_rect *r)
{
	r->x = video_width();
	r->y = 0U;
	r->w = SURFACE_W - r->x;
	r->h = SURFACE_H;
}

bool aa_layout_map_touch(uint32_t px, uint32_t py, uint32_t *vx, uint32_t *vy)
{
	struct aa_rect v;

	aa_layout_video(&v);

	if (px < v.x || py < v.y || px >= (uint32_t)v.x + v.w || py >= (uint32_t)v.y + v.h) {
		return false;
	}

	*vx = (px - v.x) * STREAM_W / v.w;
	*vy = (py - v.y) * STREAM_H / v.h;

	return true;
}
