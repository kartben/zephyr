/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_LAYOUT_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/devicetree.h>

/*
 * The surface the head unit composes, which is the panel's own size: handing
 * the display driver anything smaller makes it copy the frame into a buffer of
 * that size instead of scanning ours where it lies. A board whose panel does
 * not say how large it is gets a surface the size of the stream, which is what
 * the sample did throughout before.
 */
#if DT_HAS_CHOSEN(zephyr_display) && DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_display), width) &&         \
	DT_NODE_HAS_PROP(DT_CHOSEN(zephyr_display), height)
#define SURFACE_W DT_PROP(DT_CHOSEN(zephyr_display), width)
#define SURFACE_H DT_PROP(DT_CHOSEN(zephyr_display), height)
#else
#define SURFACE_W CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH
#define SURFACE_H CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT
#endif

/* What the phone is asked to send, which the surface is not obliged to match */
#define STREAM_W CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH
#define STREAM_H CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT

/** Rectangle of the display, in pixels from its top left corner. */
struct aa_rect {
	uint16_t x;
	uint16_t y;
	uint16_t w;
	uint16_t h;
};

/**
 * @brief Where the phone's picture goes on the display right now.
 *
 * The rectangle is the whole display in full screen mode and its top left
 * quarter in split screen mode, and moves between the two while the change is
 * being animated. Its width and height are always even, which is what the
 * packed YUV format the display controller scans needs.
 */
void aa_layout_video(struct aa_rect *r);

/**
 * @brief The part of the surface the phone's picture owns.
 *
 * The picture sits in the middle of this and never fills it unless it is
 * exactly as large; what surrounds it belongs to neither the picture nor the
 * GUI and is left dark.
 */
void aa_layout_area(struct aa_rect *r);

/**
 * @brief Where the head unit's own GUI goes on the display right now.
 *
 * The rectangle starts at the right edge of the video and runs to the right
 * edge of the display, so the two tile the width between them. It is empty in
 * full screen mode and the right half of the display in split screen mode.
 */
void aa_layout_gui(struct aa_rect *r);

/** @brief Whether any part of the GUI is on the display. */
bool aa_layout_gui_visible(void);

/** @brief Whether the layout is between the two modes. */
bool aa_layout_moving(void);

/** @brief Switch between full screen and split screen. */
void aa_layout_toggle(void);

/**
 * @brief Advance the animation.
 *
 * @param elapsed_ms Time since the last call.
 *
 * @return true when the layout changed and the display has to be redrawn.
 */
bool aa_layout_step(uint32_t elapsed_ms);

/**
 * @brief Map a touch on the display to the picture the phone sent.
 *
 * @param px,py Where the display was touched.
 * @param vx,vy Filled in with the same point in the phone's coordinates.
 *
 * @return true when the touch was on the video, false when it belongs to the
 *         GUI or to the empty area below the video.
 */
bool aa_layout_map_touch(uint32_t px, uint32_t py, uint32_t *vx, uint32_t *vy);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_LAYOUT_H_ */
