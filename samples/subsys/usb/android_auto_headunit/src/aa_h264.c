/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_h264.h"

#define SAMPLE_AA_HU_YUV_W CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH
#define SAMPLE_AA_HU_YUV_H CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
#include <zephyr/drivers/display/stm32_ltdc.h>
#endif
#include <zephyr/logging/log.h>

#include <h264bsd_decoder.h>

#include "aa_mem.h"

LOG_MODULE_REGISTER(aa_h264, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Baseline H.264 decoder, which is the profile a phone streams. The decoder
 * keeps its reference pictures on a heap of its own so that they land in
 * external RAM, far more than the internal RAM holds.
 */

static uint8_t decoder_arena[CONFIG_SAMPLE_AA_HU_H264_HEAP_SIZE] AA_HU_BIG_BUF __aligned(8);
static struct k_heap decoder_heap;
static storage_t *decoder;
static uint16_t *framebuffer;
static uint16_t fb_width;
static uint16_t fb_height;

/* The decoder library's allocator, redirected onto the heap above */
void *aa_h264_malloc(size_t size)
{
	return k_heap_alloc(&decoder_heap, size, K_NO_WAIT);
}

void aa_h264_free(void *ptr)
{
	if (ptr != NULL) {
		k_heap_free(&decoder_heap, ptr);
	}
}

int aa_h264_init(uint16_t *fb, uint16_t width, uint16_t height)
{
	framebuffer = fb;
	fb_width = width;
	fb_height = height;

	k_heap_init(&decoder_heap, decoder_arena, sizeof(decoder_arena));

	decoder = h264bsdAlloc();
	if (decoder == NULL) {
		LOG_ERR("Out of memory for the decoder");
		return -ENOMEM;
	}

	if (h264bsdInit(decoder, 0U) != H264BSD_RDY) {
		LOG_ERR("Decoder init failed");
		h264bsdFree(decoder);
		decoder = NULL;
		return -ENOMEM;
	}

	return 0;
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

	return aa_h264_init(framebuffer, fb_width, fb_height);
}

#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
#define YUV_PICTURE_SIZE \
	((size_t)CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH * CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT * 2U)

/* Keep the scanned frame separate from both the decoder and the next frame. */
static uint8_t yuv_shown[2][YUV_PICTURE_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_YUV_BUFFERS_SECTION) __aligned(32);
static uint8_t yuv_next;

#endif

static void show_yuv(const uint8_t *pic, uint32_t width, uint32_t height)
{
#ifdef CONFIG_SAMPLE_AA_HU_LTDC_YUV
	const uint8_t *u = pic + (size_t)width * height;
	const uint8_t *v = u + (size_t)width * height / 4U;
	uint8_t *dst;
	int ret;

	if (width != CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH ||
	    height != CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT ||
	    (width % 2U) != 0U || (height % 2U) != 0U) {
		LOG_WRN_ONCE("YUV picture dimensions do not match the display");
		return;
	}

	dst = yuv_shown[yuv_next];
	yuv_next ^= 1U;

	/* Interleave I420 as YUYV; the LTDC performs the color conversion. */
	for (uint32_t y = 0U; y < height; y++) {
		const uint8_t *luma = pic + (size_t)y * width;
		const uint8_t *cb = u + (size_t)(y / 2U) * (width / 2U);
		const uint8_t *cr = v + (size_t)(y / 2U) * (width / 2U);
		uint32_t *row = (uint32_t *)(dst + (size_t)y * width * 2U);

		for (uint32_t x = 0U; x < width; x += 2U) {
			uint32_t pair = (uint32_t)luma[x] | ((uint32_t)cb[x / 2U] << 8U) |
					((uint32_t)luma[x + 1U] << 16U) |
					((uint32_t)cr[x / 2U] << 24U);

			row[x / 2U] = sys_cpu_to_le32(pair);
		}
	}

	ret = stm32_ltdc_set_yuyv_frame(DEVICE_DT_GET(DT_CHOSEN(zephyr_display)), dst,
					YUV_PICTURE_SIZE);
	if (ret != 0) {
		LOG_WRN_ONCE("Display cannot show YUV (%d)", ret);
	}
#else
	ARG_UNUSED(pic);
	ARG_UNUSED(width);
	ARG_UNUSED(height);
#endif
}

/* Convert BT.601 limited-range samples for displays without YUV scanout. */
static void picture_to_rgb565(const uint8_t *pic, uint32_t width, uint32_t height)
{
	const uint8_t *luma = pic;
	const uint8_t *cb = pic + (size_t)width * height;
	const uint8_t *cr = cb + (size_t)width * height / 4U;
	uint32_t rows = MIN(height, fb_height);
	uint32_t cols = MIN(width, fb_width);

	for (uint32_t y = 0; y < rows; y++) {
		const uint8_t *y_row = &luma[(size_t)y * width];
		const uint8_t *cb_row = &cb[(size_t)(y / 2U) * (width / 2U)];
		const uint8_t *cr_row = &cr[(size_t)(y / 2U) * (width / 2U)];
		uint16_t *out = &framebuffer[(size_t)y * fb_width];

		for (uint32_t x = 0; x < cols; x++) {
			int32_t c = (int32_t)y_row[x] - 16;
			int32_t d = (int32_t)cb_row[x / 2U] - 128;
			int32_t e = (int32_t)cr_row[x / 2U] - 128;
			int32_t r = (298 * c + 409 * e + 128) >> 8;
			int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
			int32_t b = (298 * c + 516 * d + 128) >> 8;

			r = CLAMP(r, 0, 255);
			g = CLAMP(g, 0, 255);
			b = CLAMP(b, 0, 255);

			out[x] = (uint16_t)(((uint32_t)r & 0xF8U) << 8 |
					    ((uint32_t)g & 0xFCU) << 3 | (uint32_t)b >> 3);
		}
	}
}

int aa_h264_decode_au(const uint8_t *au, size_t len)
{
	uint8_t *p = (uint8_t *)au;
	uint32_t left = (uint32_t)len;
	bool stalled = false;
	int ready = 0;

	if (decoder == NULL) {
		return -EINVAL;
	}

	while (left > 0U) {
		uint32_t read = 0;
		uint32_t ret = h264bsdDecode(decoder, p, left, 0U, &read);

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

				if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_LTDC_YUV)) {
					show_yuv(pic, w, h);
				} else {
					picture_to_rgb565(pic, w, h);
				}
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
			LOG_ERR("Decoder ran out of memory");
			return -EINVAL;
		default:
			break;
		}
	}

	return ready;
}
