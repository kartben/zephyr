/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_screen.h"

#include <errno.h>
#include <string.h>

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
static uint16_t framebuffer[DISPLAY_W * DISPLAY_H] AA_HU_BIG_BUF;
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
 * The strip below the video is black and stays black, so it only has to be
 * written when the layout moves it. Filling it with every picture cost as many
 * stores as the scaled picture itself, which is most of what split screen was
 * meant to save.
 */
static struct aa_rect banded[AA_SCREEN_BUFFERS];

static void blit(void)
{
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(framebuffer),
		.width = DISPLAY_W,
		.height = DISPLAY_H,
		.pitch = DISPLAY_W,
	};

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
	if (pic != NULL) {
		aa_scale_i420_rgb565(framebuffer, DISPLAY_W, &video, pic, w, h);
	} else {
		aa_scale_fill_rgb565(framebuffer, DISPLAY_W, &video);
	}
	if (band.h != 0U && memcmp(&banded[0], &band, sizeof(band)) != 0) {
		aa_scale_fill_rgb565(framebuffer, DISPLAY_W, &band);
	}
	banded[0] = band;
	aa_gui_apply_rgb565(framebuffer, DISPLAY_W, 0U);

	blit();
	(void)hu_fb_dump_write(framebuffer, DISPLAY_W * DISPLAY_H);
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
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565) {
		LOG_WRN("Display is not RGB565, the picture may look wrong");
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
	return framebuffer;
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
	(void)hu_fb_dump_write(framebuffer, DISPLAY_W * DISPLAY_H);
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
