/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_demo.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aa_demo_clip.h"
#include "aa_h264.h"
#include "aa_input.h"
#include "aa_video.h"

LOG_MODULE_REGISTER(aa_demo, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * A phone's video channel, without the phone. The clip in aa_demo_clip.h is an
 * Annex B elementary stream built into the image, and this hands it to the
 * decoder a picture at a time on a thread of its own, which is what the
 * receive thread does with the pictures a phone sends. Everything after the
 * decoder - the scaling, the layout and the head unit's own GUI - is the same
 * code either way, so the display shows what a phone would make it show.
 *
 * The clip starts on an IDR picture and holds no other, so playing it again
 * from the start needs nothing of the decoder.
 *
 * A picture is copied out of the clip before it is decoded. The decoder is
 * given a buffer it may rewrite - it takes the bytes that keep a payload from
 * looking like a start code back out of it - and the clip is in read only
 * memory, and would not survive the second time round if it were not.
 */

#define FRAME_PERIOD_MS (1000U / AA_DEMO_CLIP_FPS)
#define CLIP_SIZE       sizeof(aa_demo_clip)

static K_THREAD_STACK_DEFINE(clip_stack, CONFIG_SAMPLE_AA_HU_DEMO_CLIP_STACK_SIZE);
static struct k_thread clip_thread_data;
static uint8_t picture[AA_DEMO_CLIP_MAX_AU];

/* Annex B separates NAL units by three or four bytes, the last of them one */
static bool start_code(size_t at, size_t *len)
{
	if ((at + 3U) > CLIP_SIZE || aa_demo_clip[at] != 0U || aa_demo_clip[at + 1U] != 0U) {
		return false;
	}

	if (aa_demo_clip[at + 2U] == 1U) {
		*len = 3U;
		return true;
	}

	if ((at + 4U) <= CLIP_SIZE && aa_demo_clip[at + 2U] == 0U && aa_demo_clip[at + 3U] == 1U) {
		*len = 4U;
		return true;
	}

	return false;
}

/*
 * How much of the clip from @p off belongs to one picture. The encoder is told
 * to code a picture as a single slice, so the access unit ends with the first
 * coded slice in it and the parameter sets ahead of a key frame travel with
 * the picture they describe.
 *
 * A start code cannot appear inside a NAL unit - the encoder breaks up any
 * byte sequence that would look like one - so scanning for it needs no notion
 * of where the payload ends.
 */
static size_t picture_length(size_t off)
{
	size_t at = off;
	size_t len;

	while (at < CLIP_SIZE) {
		uint8_t type;

		if (!start_code(at, &len)) {
			at++;
			continue;
		}

		type = aa_demo_clip[at + len] & 0x1FU;
		at += len + 1U;

		/* 1 and 5 are the coded slice of a picture and of a key frame */
		if (type != 1U && type != 5U) {
			continue;
		}

		while (at < CLIP_SIZE && !start_code(at, &len)) {
			at++;
		}

		return at - off;
	}

	return CLIP_SIZE - off;
}

static void report_rate(uint32_t *shown, int64_t *since)
{
	int64_t now = k_uptime_get();
	uint32_t ms = (uint32_t)(now - *since);

	if (ms < 2000U) {
		return;
	}

	LOG_INF("Clip: %u.%u fps", (*shown * 1000U) / ms, ((*shown * 10000U) / ms) % 10U);
	*shown = 0U;
	*since = now;
}

static void clip_thread(void *p1, void *p2, void *p3)
{
	int64_t due = k_uptime_get();
	int64_t since = due;
	uint32_t shown = 0U;
	size_t off = 0U;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		size_t len = picture_length(off);
		size_t take = MIN(len, sizeof(picture));
		int64_t now;

		memcpy(picture, &aa_demo_clip[off], take);
		if (aa_h264_decode_au(picture, take) > 0) {
			shown++;
		}

		off += len;
		if (off >= CLIP_SIZE) {
			off = 0U;
		}

		report_rate(&shown, &since);

		/*
		 * Keep to the rate the clip was encoded at where there is time
		 * to spare, and to whatever the decoder manages where there is
		 * not, rather than trying to catch up on what it has missed.
		 *
		 * Give the processor up either way. This thread runs at the
		 * priority the receive thread would, which is above the GUI and
		 * above logging, and a decoder that cannot hold the frame rate
		 * would otherwise leave neither of them a moment to run in.
		 */
		due += FRAME_PERIOD_MS;
		now = k_uptime_get();
		if (due < now) {
			due = now;
		}

		k_sleep(K_MSEC(MAX(due - now, 1)));
	}
}

int aa_hu_demo_start(void)
{
	int ret = aa_video_init();

	if (ret != 0) {
		return ret;
	}

	/*
	 * The touch screen still belongs to the display it is on, so take it
	 * even though there is no phone to forward to: a touch is mapped back
	 * to the picture it landed on either way, and the channel simply never
	 * opens.
	 */
	ret = aa_input_init();
	if (ret != 0) {
		return ret;
	}

	k_thread_create(&clip_thread_data, clip_stack, K_THREAD_STACK_SIZEOF(clip_stack),
			clip_thread, NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_RX_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&clip_thread_data, "aa_hu_clip");

	LOG_INF("Android Auto head unit ready (playing the built in clip, %ux%u)",
		AA_DEMO_CLIP_WIDTH, AA_DEMO_CLIP_HEIGHT);

	return 0;
}
