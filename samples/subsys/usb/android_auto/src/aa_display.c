/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT android_auto_display

#include "aa_display.h"

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(aa_display, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Virtual display: LVGL renders into a framebuffer kept here; the encoder
 * converts changed 16x16 stream macroblocks from it on demand. With a
 * framebuffer half the stream size every framebuffer pixel becomes a 2x2
 * block in the stream.
 */

#define AA_LV_W     DT_INST_PROP(0, width)
#define AA_LV_H     DT_INST_PROP(0, height)
#define AA_SCALE    (AA_VIDEO_WIDTH / AA_LV_W)
#define AA_BPP      (IS_ENABLED(CONFIG_SAMPLE_AA_PIXEL_L8) ? 1U : 2U)
/* Framebuffer pixels per macroblock edge */
#define AA_MB_LV_PX (16U / AA_SCALE)

BUILD_ASSERT(AA_VIDEO_WIDTH % 16 == 0 && AA_VIDEO_HEIGHT % 16 == 0,
	     "Stream resolution must be a multiple of 16");
BUILD_ASSERT(AA_SCALE == 1 || AA_SCALE == 2, "Framebuffer must be the stream size or half of it");
BUILD_ASSERT(AA_LV_W * AA_SCALE == AA_VIDEO_WIDTH && AA_LV_H * AA_SCALE == AA_VIDEO_HEIGHT,
	     "Framebuffer and stream resolutions do not match");

#ifdef CONFIG_SAMPLE_AA_FB_IN_EXT_RAM
#define AA_FB_SECTION __attribute__((section(CONFIG_SAMPLE_AA_FB_SECTION)))
#else
#define AA_FB_SECTION
#endif

static uint8_t aa_fb[AA_LV_W * AA_LV_H * AA_BPP] AA_FB_SECTION __aligned(32);
static ATOMIC_DEFINE(aa_dirty, AA_MBS);
static K_SEM_DEFINE(aa_frame_sem, 0, 1);
static atomic_t aa_dirty_since_frame;

static const struct aa_display_geometry aa_geometry = {
	.lv_width = AA_LV_W,
	.lv_height = AA_LV_H,
	.width = AA_VIDEO_WIDTH,
	.height = AA_VIDEO_HEIGHT,
	.scale = AA_SCALE,
	.bpp = AA_BPP,
	.mbs_x = AA_MBS_X,
	.mbs_y = AA_MBS_Y,
};

const struct aa_display_geometry *aa_display_geometry(void)
{
	return &aa_geometry;
}

const uint8_t *aa_display_framebuffer(void)
{
	return aa_fb;
}

atomic_t *aa_display_dirty(void)
{
	return aa_dirty;
}

int aa_display_wait_frame(k_timeout_t timeout)
{
	if (k_sem_take(&aa_frame_sem, timeout) != 0) {
		return -EAGAIN;
	}

	return 0;
}

uint32_t aa_display_dirty_count(void)
{
	uint32_t count = 0;

	for (uint32_t mb = 0; mb < AA_MBS; mb++) {
		if (atomic_test_bit(aa_dirty, mb)) {
			count++;
		}
	}

	return count;
}

static int aa_display_write(const struct device *dev, const uint16_t x, const uint16_t y,
			    const struct display_buffer_descriptor *desc, const void *buf)
{
	const uint8_t *src = buf;
	size_t src_stride = (size_t)desc->pitch * AA_BPP;
	bool changed = false;

	if (desc->width > desc->pitch || (x + desc->width) > AA_LV_W ||
	    (y + desc->height) > AA_LV_H || desc->buf_size < src_stride * desc->height) {
		LOG_ERR("Invalid write %ux%u at %u,%u pitch %u size %u", desc->width,
			desc->height, x, y, desc->pitch, desc->buf_size);
		return -EINVAL;
	}

	for (uint32_t row = 0; row < desc->height; row++) {
		uint32_t fb_row = y + row;
		uint8_t *dst = &aa_fb[(fb_row * AA_LV_W + x) * AA_BPP];
		const uint8_t *s = src + row * src_stride;
		uint32_t mb_row = (fb_row * AA_SCALE) / 16U;
		uint32_t col = x;
		uint32_t end = x + desc->width;

		while (col < end) {
			uint32_t mb_col = (col * AA_SCALE) / 16U;
			uint32_t seg_end = MIN((mb_col + 1U) * AA_MB_LV_PX, end);
			size_t off = (col - x) * AA_BPP;
			size_t len = (seg_end - col) * AA_BPP;

			if (!IS_ENABLED(CONFIG_SAMPLE_AA_DIRTY_COMPARE) ||
			    memcmp(dst + off, s + off, len) != 0) {
				memcpy(dst + off, s + off, len);
				atomic_set_bit(aa_dirty, mb_row * AA_MBS_X + mb_col);
				changed = true;
			}
			col = seg_end;
		}
	}

	if (changed) {
		atomic_set(&aa_dirty_since_frame, 1);
	}

	if (!desc->frame_incomplete && atomic_cas(&aa_dirty_since_frame, 1, 0)) {
		k_sem_give(&aa_frame_sem);
	}

	return 0;
}

static int aa_display_blanking(const struct device *dev)
{
	return 0;
}

static void aa_display_get_capabilities(const struct device *dev,
					struct display_capabilities *caps)
{
	memset(caps, 0, sizeof(*caps));
	caps->x_resolution = AA_LV_W;
	caps->y_resolution = AA_LV_H;
	if (IS_ENABLED(CONFIG_SAMPLE_AA_PIXEL_L8)) {
		caps->supported_pixel_formats = PIXEL_FORMAT_L_8;
		caps->current_pixel_format = PIXEL_FORMAT_L_8;
	} else {
		caps->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
		caps->current_pixel_format = PIXEL_FORMAT_RGB_565;
	}
	caps->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

static int aa_display_set_pixel_format(const struct device *dev,
				       const enum display_pixel_format format)
{
	struct display_capabilities caps;

	aa_display_get_capabilities(dev, &caps);

	return (format == caps.current_pixel_format) ? 0 : -ENOTSUP;
}

static int aa_display_init(const struct device *dev)
{
	LOG_INF("Virtual display %ux%u (%s), stream %ux%u, scale %u", AA_LV_W, AA_LV_H,
		IS_ENABLED(CONFIG_SAMPLE_AA_PIXEL_L8) ? "L8" : "RGB565", AA_VIDEO_WIDTH,
		AA_VIDEO_HEIGHT, AA_SCALE);

	return 0;
}

static DEVICE_API(display, aa_display_api) = {
	.blanking_on = aa_display_blanking,
	.blanking_off = aa_display_blanking,
	.write = aa_display_write,
	.get_capabilities = aa_display_get_capabilities,
	.set_pixel_format = aa_display_set_pixel_format,
};

DEVICE_DT_INST_DEFINE(0, aa_display_init, NULL, NULL, NULL, POST_KERNEL,
		      CONFIG_DISPLAY_INIT_PRIORITY, &aa_display_api);
