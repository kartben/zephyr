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
#define FB_PIXELS ((size_t)SURFACE_W * SURFACE_H)
#define FB_BYTES  (FB_PIXELS * sizeof(surface_px))

/*
 * The controller is handed a buffer and scans it until it is handed another,
 * so composing into the one on the panel shows the frame being drawn. Keep
 * two and compose into whichever the controller is not reading.
 */
static surface_px fb_store[2][FB_PIXELS] Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_FB_SECTION);
static surface_px *framebuffer = fb_store[0];
static uint8_t fb_idx;
/* The decoder thread and the GUI thread both compose into the surface */
static K_MUTEX_DEFINE(lock);
static int64_t last_picture;

/*
 * Handing a buffer to the controller means waiting for the panel to start
 * scanning it, which rounds every frame up to a whole refresh period. That
 * wait runs on a thread of its own, so the decoder is busy with the next
 * picture while it happens.
 *
 * fb_free counts the buffers that may be drawn into. A present completing
 * releases the one the panel was showing before it, so claim a buffer
 * before writing into the surface and let blit() give it away.
 */
static K_SEM_DEFINE(fb_free, 1, 1);
static K_SEM_DEFINE(fb_queued, 0, 1);
static surface_px *fb_pending;
static struct k_thread present_thread_data;
static K_THREAD_STACK_DEFINE(present_stack, CONFIG_SAMPLE_AA_HU_PRESENT_STACK_SIZE);
/* Held across every call into the display, which the presenter also makes */
static K_MUTEX_DEFINE(disp);

#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
#define YUV_PICTURE_SIZE ((size_t)SURFACE_W * SURFACE_H * 2U)

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
static uint32_t wait_us;
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

	LOG_INF("Per picture: convert %u us, push %u us, wait %u us", convert_us / composed,
		push_us / composed, wait_us / composed);
	convert_us = 0U;
	push_us = 0U;
	wait_us = 0U;
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
 * Where the picture was last drawn in each buffer. What surrounds it is black
 * and stays black, so it only has to be written when the picture moves.
 * Filling it with every picture cost as many stores as the picture itself,
 * which is most of what split screen was meant to save.
 */
static struct aa_rect framed[AA_SCREEN_BUFFERS];

#ifndef CONFIG_SAMPLE_AA_HU_LTDC_YUV
/*
 * The three writers the surface needs, picked by its pixel width so that
 * compose() reads the same either way.
 */
#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888

static void surface_picture(const struct aa_rect *r, const uint8_t *pic, uint16_t w, uint16_t h)
{
	aa_scale_i420_argb8888(framebuffer, SURFACE_W, r, pic, w, h);
}

static void surface_fill(const struct aa_rect *r)
{
	aa_scale_fill_argb8888(framebuffer, SURFACE_W, r);
}

static void surface_gui(unsigned int idx)
{
	aa_gui_apply_argb8888(framebuffer, SURFACE_W, idx);
}

#else

static void surface_picture(const struct aa_rect *r, const uint8_t *pic, uint16_t w, uint16_t h)
{
	aa_scale_i420_rgb565(framebuffer, SURFACE_W, r, pic, w, h);
}

static void surface_fill(const struct aa_rect *r)
{
	aa_scale_fill_rgb565(framebuffer, SURFACE_W, r);
}

static void surface_gui(unsigned int idx)
{
	aa_gui_apply_rgb565(framebuffer, SURFACE_W, idx);
}

#endif /* CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888 */
#endif /* CONFIG_SAMPLE_AA_HU_LTDC_YUV */

/* The dump is an RGB565 stream, and Kconfig only offers it where the surface is one */
static void surface_dump(const surface_px *fb)
{
	(void)hu_fb_dump_write((const uint16_t *)fb, SURFACE_W * SURFACE_H);
}

/* Wait for the surface to be free, which it is once the panel has moved on */
static void surface_claim(void)
{
	k_sem_take(&fb_free, K_FOREVER);
}

static surface_px *blit(void)
{
	surface_px *shown = framebuffer;

	/*
	 * The display controller reads the framebuffer out of memory itself,
	 * so what composing it left behind in the cache has to reach memory
	 * before it does. Without a data cache this costs nothing, and where
	 * the surface is uncached there is nothing to write back.
	 */
	if (!IS_ENABLED(CONFIG_SAMPLE_AA_HU_FB_UNCACHED)) {
		sys_cache_data_flush_range(shown, FB_BYTES);
	}

	fb_pending = shown;
	k_sem_give(&fb_queued);

	/*
	 * The panel goes on showing the other buffer until this one reaches
	 * it, so whoever draws next claims it first. Between them the area the
	 * video owns and the GUI cover the surface, and both remember per
	 * buffer what they last drew, so nothing of the older frame shows
	 * through.
	 */
	fb_idx ^= 1U;
	framebuffer = fb_store[fb_idx];

	return shown;
}

static void present_thread(void *p1, void *p2, void *p3)
{
	struct display_buffer_descriptor desc = {
		.buf_size = FB_BYTES,
		.width = SURFACE_W,
		.height = SURFACE_H,
		.pitch = SURFACE_W,
	};

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (true) {
		const surface_px *shown;

		k_sem_take(&fb_queued, K_FOREVER);
		shown = fb_pending;

		k_mutex_lock(&disp, K_FOREVER);
		(void)display_write(display, 0, 0, &desc, shown);
		k_mutex_unlock(&disp);

		/*
		 * The write returns once the panel is scanning this buffer,
		 * which is what frees the one it was scanning before.
		 */
		k_sem_give(&fb_free);
	}
}

/*
 * The video keeps the left of the surface and the GUI takes what is left over,
 * so between them they cover it. The picture sits in the middle of what the
 * video owns at the size the phone sent it, so the area around it is filled
 * whenever the picture moves.
 */
static void compose(const uint8_t *pic, uint16_t w, uint16_t h)
{
#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	uint8_t idx = yuv_next;
	uint8_t *dst = yuv_shown[idx];
#endif
	struct aa_rect video;
	struct aa_rect area;
	uint32_t at;
#ifndef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	surface_px *shown;
#endif

	report_timing();
	aa_layout_video(&video);
	aa_layout_area(&area);

#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	yuv_next ^= 1U;

	if (pic != NULL) {
		aa_scale_i420_yuyv(dst, SURFACE_W, &video, pic, w, h);
	} else {
		aa_scale_fill_yuyv(dst, SURFACE_W, &video);
	}
	if (memcmp(&framed[idx], &video, sizeof(video)) != 0) {
		aa_scale_fill_yuyv(dst, SURFACE_W, &area);
		framed[idx] = video;
	}
	aa_gui_apply_yuyv(dst, SURFACE_W, idx);

	if (stm32_ltdc_set_yuyv_frame(display, dst, YUV_PICTURE_SIZE) != 0) {
		LOG_WRN_ONCE("Display cannot show YUV");
	}
#else
	at = stamp();
	surface_claim();
	elapsed(&wait_us, at);

	at = stamp();
	if (pic != NULL) {
		surface_picture(&video, pic, w, h);
	} else {
		surface_fill(&video);
	}
	if (memcmp(&framed[fb_idx], &video, sizeof(video)) != 0) {
		surface_fill(&area);
		framed[fb_idx] = video;
	}
	surface_gui(fb_idx);
	elapsed(&convert_us, at);

	at = stamp();
	shown = blit();
	elapsed(&push_us, at);
	composed++;
	surface_dump(shown);
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

	k_thread_create(&present_thread_data, present_stack,
			K_THREAD_STACK_SIZEOF(present_stack), present_thread, NULL, NULL, NULL,
			CONFIG_SAMPLE_AA_HU_PRESENT_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&present_thread_data, "aa_hu_present");

	display_get_capabilities(display, &caps);
	if (caps.x_resolution != SURFACE_W || caps.y_resolution != SURFACE_H) {
		LOG_WRN("Display is %ux%u, surface is %ux%u: the driver will copy every frame",
			caps.x_resolution, caps.y_resolution, SURFACE_W, SURFACE_H);
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
	surface_claim();
	memset(framebuffer, 0, FB_BYTES);
#ifdef CONFIG_SAMPLE_AA_HU_TEST_PATTERN
	/* Colour bars, to check the panel and the pixel format */
	for (uint32_t y = 0; y < SURFACE_H; y++) {
		for (uint32_t x = 0; x < SURFACE_W; x++) {
			static const uint16_t bars[] = {0xFFFFU, 0xFFE0U, 0x07FFU, 0x07E0U,
							0xF81FU, 0xF800U, 0x001FU, 0x0000U};

			framebuffer[y * SURFACE_W + x] = bars[(x * ARRAY_SIZE(bars)) / SURFACE_W];
		}
	}
#endif
	LOG_INF("Framebuffer %p pushed to the display", (void *)blit());

	/*
	 * Let it reach the panel before the backlight comes on, so the first
	 * thing shown is the blank surface rather than whatever the memory
	 * happened to hold.
	 */
	surface_claim();
	k_sem_give(&fb_free);

	k_mutex_lock(&disp, K_FOREVER);
	(void)display_blanking_off(display);
	k_mutex_unlock(&disp);
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

	surface_claim();
	surface_dump(blit());
	last_picture = k_uptime_get();

	k_mutex_unlock(&lock);
}

void aa_screen_blank(bool on)
{
	k_mutex_lock(&lock, K_FOREVER);

	if (!on) {
		k_mutex_lock(&disp, K_FOREVER);
		(void)display_blanking_off(display);
		k_mutex_unlock(&disp);
	} else if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_LTDC_YUV)) {
		/*
		 * The layer is showing the decoder's picture, not a buffer of
		 * ours, so blank the panel instead of overwriting one.
		 */
		k_mutex_lock(&disp, K_FOREVER);
		(void)display_blanking_on(display);
		k_mutex_unlock(&disp);
	} else {
		surface_claim();
		memset(framebuffer, 0, FB_BYTES);
		(void)blit();
	}

	k_mutex_unlock(&lock);
}

int64_t aa_screen_last_picture(void)
{
	return last_picture;
}
