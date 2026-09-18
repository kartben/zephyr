/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_screen.h"

#include <errno.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
#include <zephyr/drivers/display/stm32_ltdc.h>
#endif
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "aa_gui.h"
#include "aa_layout.h"
#include "aa_mem.h"
#include "aa_scale.h"
#include "hu_fb_dump.h"

LOG_MODULE_REGISTER(aa_screen, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

#define DISPLAY_W CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH
#define DISPLAY_H CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT

static const struct device *display;
/*
 * A word per pixel where the display scans nothing narrower, which is what the
 * QEMU ramfb the browser emulator paints is fixed at; two bytes otherwise.
 */
#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888
typedef uint32_t surface_px;
#define SURFACE_FORMAT       PIXEL_FORMAT_ARGB_8888
#define SURFACE_RGB(r, g, b) (0xFF000000U | ((r) << 16) | ((g) << 8) | (b))
#else
typedef uint16_t surface_px;
#define SURFACE_FORMAT       PIXEL_FORMAT_RGB_565
#define SURFACE_RGB(r, g, b)                                                                       \
	((surface_px)(((r) & 0xF8U) << 8 | ((g) & 0xFCU) << 3 | (b) >> 3))
#endif
static surface_px framebuffer[DISPLAY_W * DISPLAY_H] AA_HU_BIG_BUF;
/* The decoder thread and the GUI thread both compose into the surface */
static K_MUTEX_DEFINE(lock);
static int64_t last_picture;

#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
#define YUV_PICTURE_SIZE ((size_t)DISPLAY_W * DISPLAY_H * 2U)

/* Keep the scanned frame separate from both the decoder and the next frame. */
static uint8_t yuv_shown[AA_SCREEN_BUFFERS][YUV_PICTURE_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_YUV_BUFFERS_SECTION) __aligned(32);
static uint8_t yuv_next;
#endif

/*
 * Where a picture's time goes once the decoder is done with it. aa_h264 times
 * the two halves of a picture, decode and display; this splits the second of
 * them into the conversion into the surface and the handover to the driver,
 * which are the two things that can be made faster independently.
 */
static uint32_t convert_us;
static uint32_t push_us;
static uint32_t composed;

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

	if (composed == 0U || (now - since) < 2000) {
		return;
	}

	LOG_INF("Per picture: convert %u us, push %u us", convert_us / composed,
		push_us / composed);
	convert_us = 0U;
	push_us = 0U;
	composed = 0U;
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
 * The strip below the video is black and stays black, so it only has to be
 * written when the layout moves it. Filling it with every picture cost as many
 * stores as the scaled picture itself, which is most of what split screen was
 * meant to save.
 */
static struct aa_rect banded[AA_SCREEN_BUFFERS];

#ifndef CONFIG_SAMPLE_AA_HU_LTDC_YUV
/*
 * The three writers the surface needs, picked by its pixel width so that
 * compose() reads the same either way.
 */
#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888

static void surface_picture(const struct aa_rect *r, const uint8_t *pic, uint16_t w, uint16_t h)
{
	aa_scale_i420_argb8888(framebuffer, DISPLAY_W, r, pic, w, h);
}

static void surface_fill(const struct aa_rect *r)
{
	aa_scale_fill_argb8888(framebuffer, DISPLAY_W, r);
}

static void surface_gui(unsigned int idx)
{
	aa_gui_apply_argb8888(framebuffer, DISPLAY_W, idx);
}

#else

static void surface_picture(const struct aa_rect *r, const uint8_t *pic, uint16_t w, uint16_t h)
{
	aa_scale_i420_rgb565(framebuffer, DISPLAY_W, r, pic, w, h);
}

static void surface_fill(const struct aa_rect *r)
{
	aa_scale_fill_rgb565(framebuffer, DISPLAY_W, r);
}

static void surface_gui(unsigned int idx)
{
	aa_gui_apply_rgb565(framebuffer, DISPLAY_W, idx);
}

#endif /* CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888 */
#endif /* CONFIG_SAMPLE_AA_HU_LTDC_YUV */

/* The dump is an RGB565 stream, and Kconfig only offers it where the surface is one */
static void surface_dump(void)
{
	(void)hu_fb_dump_write((const uint16_t *)framebuffer, DISPLAY_W * DISPLAY_H);
}

static void blit(void)
{
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(framebuffer),
		.width = DISPLAY_W,
		.height = DISPLAY_H,
		.pitch = DISPLAY_W,
	};

	/*
	 * The display controller reads the framebuffer out of memory itself,
	 * so what composing it left behind in the cache has to reach memory
	 * before it does. Without a data cache this costs nothing.
	 */
	sys_cache_data_flush_range(framebuffer, sizeof(framebuffer));

	(void)display_write(display, 0, 0, &desc, framebuffer);
}

/*
 * The video keeps the left of the display and the GUI takes what is left over,
 * so between them they cover the width. What the video does not reach below
 * itself is the only part of the display neither owns.
 */
static void compose(const uint8_t *pic, uint16_t w, uint16_t h)
{
#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	uint8_t idx = yuv_next;
	uint8_t *dst = yuv_shown[idx];
#endif
	struct aa_rect video;
	struct aa_rect band;
	uint32_t at;

	report_timing();
	aa_layout_video(&video);
	band.x = 0U;
	band.y = video.h;
	band.w = video.w;
	band.h = DISPLAY_H - video.h;

#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	yuv_next ^= 1U;

	if (pic != NULL) {
		aa_scale_i420_yuyv(dst, DISPLAY_W, &video, pic, w, h);
	} else {
		aa_scale_fill_yuyv(dst, DISPLAY_W, &video);
	}
	if (band.h != 0U && memcmp(&banded[idx], &band, sizeof(band)) != 0) {
		aa_scale_fill_yuyv(dst, DISPLAY_W, &band);
	}
	banded[idx] = band;
	aa_gui_apply_yuyv(dst, DISPLAY_W, idx);

	if (stm32_ltdc_set_yuyv_frame(display, dst, YUV_PICTURE_SIZE) != 0) {
		LOG_WRN_ONCE("Display cannot show YUV");
	}
#else
	at = stamp();
	if (pic != NULL) {
		surface_picture(&video, pic, w, h);
	} else {
		surface_fill(&video);
	}
	if (band.h != 0U && memcmp(&banded[0], &band, sizeof(band)) != 0) {
		surface_fill(&band);
	}
	banded[0] = band;
	surface_gui(0U);
	elapsed(&convert_us, at);

	at = stamp();
	blit();
	elapsed(&push_us, at);
	composed++;
	surface_dump();
#endif
}

int aa_screen_init(void)
{
	struct display_capabilities caps;

	display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display)) {
		LOG_ERR("Display not ready");
		return -ENODEV;
	}

	display_get_capabilities(display, &caps);
	if (caps.x_resolution != DISPLAY_W || caps.y_resolution != DISPLAY_H) {
		LOG_WRN("Display is %ux%u, stream is %ux%u", caps.x_resolution,
			caps.y_resolution, DISPLAY_W, DISPLAY_H);
	}
	if (caps.current_pixel_format != SURFACE_FORMAT) {
		LOG_WRN("Display format is %d, the picture may look wrong",
			caps.current_pixel_format);
	}

	/*
	 * External RAM is not cleared on startup the way ordinary .bss is, so
	 * blank the picture before the panel is switched on. Until the phone
	 * sends a frame the display would otherwise show whatever the memory
	 * happened to hold.
	 */
	memset(framebuffer, 0, sizeof(framebuffer));
#ifdef CONFIG_SAMPLE_AA_HU_TEST_PATTERN
	/* Colour bars, to check the panel and the pixel format */
	for (uint32_t y = 0; y < DISPLAY_H; y++) {
		for (uint32_t x = 0; x < DISPLAY_W; x++) {
			static const uint16_t bars[] = {0xFFFFU, 0xFFE0U, 0x07FFU, 0x07E0U,
							0xF81FU, 0xF800U, 0x001FU, 0x0000U};

			framebuffer[y * DISPLAY_W + x] = bars[(x * ARRAY_SIZE(bars)) / DISPLAY_W];
		}
	}
#endif
	blit();
	LOG_INF("Framebuffer %p pushed to the display", (void *)framebuffer);

	(void)display_blanking_off(display);
	(void)hu_fb_dump_open();

	return 0;
}

uint16_t *aa_screen_framebuffer(void)
{
	/*
	 * The I_PCM decoder is the only caller and writes RGB565 pixels of its
	 * own, which is why a wider surface is only offered alongside the full
	 * decoder, whose pictures are composed rather than written here.
	 */
	return (uint16_t *)framebuffer;
}

void aa_screen_show(const uint8_t *pic, uint16_t w, uint16_t h)
{
	k_mutex_lock(&lock, K_FOREVER);

	compose(pic, w, h);
	if (pic != NULL) {
		last_picture = k_uptime_get();
	}

	k_mutex_unlock(&lock);
}

void aa_screen_push(void)
{
	k_mutex_lock(&lock, K_FOREVER);

	blit();
	surface_dump();
	last_picture = k_uptime_get();

	k_mutex_unlock(&lock);
}

void aa_screen_blank(bool on)
{
	k_mutex_lock(&lock, K_FOREVER);

	if (!on) {
		(void)display_blanking_off(display);
	} else if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_LTDC_YUV)) {
		/*
		 * The layer is showing the decoder's picture, not a buffer of
		 * ours, so blank the panel instead of overwriting one.
		 */
		(void)display_blanking_on(display);
	} else {
		memset(framebuffer, 0, sizeof(framebuffer));
		blit();
	}

	k_mutex_unlock(&lock);
}

int64_t aa_screen_last_picture(void)
{
	return last_picture;
}
