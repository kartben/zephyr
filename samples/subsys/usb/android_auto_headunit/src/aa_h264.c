/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_h264.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <h264bsd_decoder.h>

#include "aa_screen.h"

LOG_MODULE_REGISTER(aa_h264, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Baseline H.264 decoder, which is the profile a phone streams. The decoder
 * keeps its reference pictures on a heap of its own, so where they live is a
 * build choice: internal RAM decodes far faster, external RAM holds more.
 */

static uint8_t decoder_arena[CONFIG_SAMPLE_AA_HU_H264_HEAP_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_H264_HEAP_SECTION) __aligned(8);
#ifdef CONFIG_SAMPLE_AA_HU_H264_HEAP_FALLBACK
static uint8_t decoder_fallback_arena[CONFIG_SAMPLE_AA_HU_H264_HEAP_FALLBACK_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_H264_HEAP_FALLBACK_SECTION) __aligned(8);
#endif
static uint8_t state_arena[CONFIG_SAMPLE_AA_HU_H264_STATE_HEAP_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_H264_STATE_HEAP_SECTION) __aligned(8);
static struct k_heap decoder_heap;
static struct k_heap state_heap;
static uint8_t *arena;
static size_t arena_size;
static storage_t *decoder;

#if defined(CONFIG_SAMPLE_AA_HU_H264_HEAP_STATS)
/* The most the decoder has held at once, against what it was given */
static void report_heap(void)
{
	static int64_t since;
	struct sys_memory_stats stats;

	if (k_uptime_get() - since < 2000) {
		return;
	}
	since = k_uptime_get();

	if (sys_heap_runtime_stats_get(&decoder_heap.heap, &stats) != 0) {
		return;
	}

	LOG_INF("Picture heap: %zu now, %zu at most, of %zu", stats.allocated_bytes,
		stats.max_allocated_bytes, arena_size);

	if (sys_heap_runtime_stats_get(&state_heap.heap, &stats) == 0) {
		LOG_INF("State heap: %zu now, %zu at most, of %zu", stats.allocated_bytes,
			stats.max_allocated_bytes, sizeof(state_arena));
	}
}
#else
static inline void report_heap(void)
{
}
#endif

/*
 * Where a picture's time goes, which is the only way to tell the decoder's
 * share from the colour conversion and the copy to the display.
 */
static uint32_t decode_us;
static uint32_t display_us;
static uint32_t pictures;

#ifdef CONFIG_SAMPLE_AA_HU_VIDEO_PROFILE
static uint32_t stamp(void)
{
	return k_cycle_get_32();
}

static void elapsed(uint32_t *acc, uint32_t from)
{
	*acc += k_cyc_to_us_floor32(k_cycle_get_32() - from);
}

static void report_timing(void)
{
	static int64_t since;
	int64_t now = k_uptime_get();

	if (pictures == 0U || (now - since) < 2000) {
		return;
	}

	LOG_INF("Per picture: decode %u us, display %u us", decode_us / pictures,
		display_us / pictures);
	decode_us = 0U;
	display_us = 0U;
	pictures = 0U;
	since = now;
}
#else
static uint32_t stamp(void)
{
	return 0U;
}

static void elapsed(uint32_t *acc, uint32_t from)
{
	ARG_UNUSED(acc);
	ARG_UNUSED(from);
}

static void report_timing(void)
{
}
#endif

/*
 * The decoder library's two allocators. Everything but the pictures comes from
 * the state heap, which is small enough to keep in internal RAM and is read
 * far more often per byte; the pictures come from the arena the board placed.
 */
void *aa_h264_malloc(size_t size)
{
	return k_heap_alloc(&state_heap, size, K_NO_WAIT);
}

void aa_h264_free(void *ptr)
{
	if (ptr != NULL) {
		k_heap_free(&state_heap, ptr);
	}
}

void *aa_h264_picture_malloc(size_t size)
{
	return k_heap_alloc(&decoder_heap, size, K_NO_WAIT);
}

void aa_h264_picture_free(void *ptr)
{
	if (ptr != NULL) {
		k_heap_free(&decoder_heap, ptr);
	}
}

static int decoder_open(uint8_t *buf, size_t size)
{
	static bool state_ready;

	arena = buf;
	arena_size = size;
	k_heap_init(&decoder_heap, buf, size);

	if (!state_ready) {
		k_heap_init(&state_heap, state_arena, sizeof(state_arena));
		state_ready = true;
	}

	decoder = h264bsdAlloc();
	if (decoder == NULL) {
		LOG_ERR("Out of memory for the decoder");
		return -ENOMEM;
	}

	/*
	 * A phone streams baseline H.264, which has no picture the decoder would
	 * have to hold back, so take every picture as it comes. The decoder then
	 * keeps the references the stream asks for rather than the number the
	 * level allows, which is most of the heap.
	 */
	if (h264bsdInit(decoder, 1U) != H264BSD_RDY) {
		LOG_ERR("Decoder init failed");
		h264bsdFree(decoder);
		decoder = NULL;
		return -ENOMEM;
	}

	return 0;
}

#ifdef CONFIG_SAMPLE_AA_HU_H264_HEAP_FALLBACK
/*
 * The stream asks for more reference pictures than the first heap holds. Start
 * again on the larger one, which decodes more slowly but at least decodes; the
 * next key frame picks the picture back up.
 */
static int decoder_downgrade(void)
{
	if (arena == decoder_fallback_arena) {
		LOG_ERR("Decoder ran out of memory");
		return -EINVAL;
	}

	LOG_WRN("Stream does not fit %zu bytes, moving the decoder to %s", arena_size,
		CONFIG_SAMPLE_AA_HU_H264_HEAP_FALLBACK_SECTION);

	if (decoder != NULL) {
		h264bsdShutdown(decoder);
		decoder = NULL;
	}

	if (decoder_open(decoder_fallback_arena, sizeof(decoder_fallback_arena)) != 0) {
		return -ENOMEM;
	}

	return -EAGAIN;
}
#else
static int decoder_downgrade(void)
{
	LOG_ERR("Decoder ran out of memory");
	return -EINVAL;
}
#endif

int aa_h264_init(void)
{
	return decoder_open(decoder_arena, sizeof(decoder_arena));
}

/*
 * A new phone brings a new stream, and the reference pictures of the old one
 * mean nothing to it. The heap is reinitialised wholesale, which is what frees
 * everything the decoder had taken from it.
 */
int aa_h264_reset(void)
{
	if (decoder != NULL) {
		h264bsdShutdown(decoder);
		decoder = NULL;
	}

	return aa_h264_init();
}

int aa_h264_decode_au(const uint8_t *au, size_t len)
{
	uint8_t *p = (uint8_t *)au;
	uint32_t left = (uint32_t)len;
	bool stalled = false;
	int ready = 0;

	report_heap();
	report_timing();

	if (decoder == NULL) {
		return -EINVAL;
	}

	while (left > 0U) {
		uint32_t read = 0;
		uint32_t at = stamp();
		uint32_t ret = h264bsdDecode(decoder, p, left, 0U, &read);

		elapsed(&decode_us, at);

		/*
		 * The decoder reports the parameter sets without consuming
		 * anything and expects the same buffer again, so a call that
		 * makes no progress is only given up on the second time.
		 */
		if (read == 0U) {
			if (stalled) {
				break;
			}
			stalled = true;
		} else {
			stalled = false;
		}

		/* The count can exceed what was offered, never walk past the end */
		read = MIN(read, left);
		p += read;
		left -= read;

		switch (ret) {
		case H264BSD_PIC_RDY: {
			uint32_t pic_id;
			uint32_t is_idr;
			uint32_t err_mbs;
			uint8_t *pic = h264bsdNextOutputPicture(decoder, &pic_id, &is_idr,
							       &err_mbs);

			if (pic != NULL) {
				uint32_t w = h264bsdPicWidth(decoder) * 16U;
				uint32_t h = h264bsdPicHeight(decoder) * 16U;

				at = stamp();
				aa_screen_show(pic, (uint16_t)w, (uint16_t)h);
				elapsed(&display_us, at);
				pictures++;
				ready = 1;
			}
			break;
		}
		case H264BSD_HDRS_RDY:
			LOG_INF("Stream is %ux%u", h264bsdPicWidth(decoder) * 16U,
				h264bsdPicHeight(decoder) * 16U);
			break;
		case H264BSD_ERROR:
		case H264BSD_PARAM_SET_ERROR:
			LOG_WRN_ONCE("Decoder reported a stream error");
			break;
		case H264BSD_MEMALLOC_ERROR:
			return decoder_downgrade();
		default:
			break;
		}
	}

	return ready;
}
