/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_scale.h"

/* Bit 0 of __ARM_FEATURE_MVE is the integer subset, which is all this needs */
#if defined(__ARM_FEATURE_MVE) && (((__ARM_FEATURE_MVE) & 1) != 0)
#define HU_HAS_MVE 1
#include <arm_mve.h>

static inline int16x8_t mve_clip255(int16x8_t v)
{
	return vminq_s16(vmaxq_s16(v, vdupq_n_s16(0)), vdupq_n_s16(255));
}
#endif

#include <stddef.h>
#include <string.h>

#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SAMPLE_AA_HU_PPA
#include <errno.h>

#include <zephyr/cache.h>
#include <zephyr/logging/log.h>

#include <driver/ppa.h>

#include "aa_mem.h"

LOG_MODULE_REGISTER(aa_scale, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);
#endif

/* Black in the limited range samples the display controller is told to expect */
#define YUYV_BLACK 0x80108010U

static inline uint8_t *yuyv_row(uint8_t *dst, uint16_t pitch, uint16_t x, uint16_t y)
{
	return dst + ((size_t)y * pitch + x) * 2U;
}

/*
 * One pixel pair of a packed YUYV row. Rectangles start on an even column of a
 * surface that is aligned itself, so every pair is a whole word.
 */
static inline void yuyv_pair(uint8_t *out, uint8_t y0, uint8_t cb, uint8_t y1, uint8_t cr)
{
	uint32_t pair = (uint32_t)y0 | ((uint32_t)cb << 8U) | ((uint32_t)y1 << 16U) |
			((uint32_t)cr << 24U);

	*(uint32_t *)out = sys_cpu_to_le32(pair);
}

/*
 * Where pixel (x, y) of the picture's frame lands in an RGB565 surface, and how
 * far apart in memory two neighbours on one of its rows are. On a panel turned
 * a quarter turn the rows of the picture run along the panel's columns, so a
 * row is written with a stride of a whole panel line.
 */
#if defined(CONFIG_SAMPLE_AA_HU_ROTATE_90)
#define RGB_ROTATED 1
#define RGB_AT(dst, pitch, x, y) ((dst) + (size_t)(x) * SCAN_W + (SCAN_W - 1U - (y)))
#define RGB_STEP ((ptrdiff_t)SCAN_W)
#define RGB_DOWN ((ptrdiff_t)-1)
#elif defined(CONFIG_SAMPLE_AA_HU_ROTATE_270)
#define RGB_ROTATED 1
#define RGB_AT(dst, pitch, x, y) ((dst) + (size_t)(SCAN_H - 1U - (x)) * SCAN_W + (y))
#define RGB_STEP (-(ptrdiff_t)SCAN_W)
#define RGB_DOWN ((ptrdiff_t)1)
#else
#define RGB_ROTATED 0
#define RGB_AT(dst, pitch, x, y) ((dst) + (size_t)(y) * (pitch) + (x))
#define RGB_STEP ((ptrdiff_t)1)
#endif

/* BT.601 limited range, the coefficients the decoder's samples are defined by */
static inline uint16_t yuv_to_rgb565(int32_t y, int32_t cb, int32_t cr)
{
	int32_t c = y - 16;
	int32_t d = cb - 128;
	int32_t e = cr - 128;
	int32_t r = (298 * c + 409 * e + 128) >> 8;
	int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int32_t b = (298 * c + 516 * d + 128) >> 8;

	r = CLAMP(r, 0, 255);
	g = CLAMP(g, 0, 255);
	b = CLAMP(b, 0, 255);

	return (uint16_t)(((uint32_t)r & 0xF8U) << 8 | ((uint32_t)g & 0xFCU) << 3 |
			  (uint32_t)b >> 3);
}

/*
 * The whole picture, one output pixel per input pixel. Interleaving the three
 * planes is all the format needs, so this is the cheapest of the three paths
 * per pixel and the most expensive per picture.
 */
static void yuyv_1_1(uint8_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
		     uint16_t w, uint16_t h)
{
	const uint8_t *u = pic + (size_t)w * h;
	const uint8_t *v = u + (size_t)w * h / 4U;

	for (uint16_t y = 0U; y < h; y++) {
		const uint8_t *luma = pic + (size_t)y * w;
		const uint8_t *cb = u + (size_t)(y / 2U) * (w / 2U);
		const uint8_t *cr = v + (size_t)(y / 2U) * (w / 2U);
		uint8_t *out = yuyv_row(dst, pitch, r->x, r->y + y);
		uint16_t x = 0U;

#ifdef HU_HAS_MVE
		/*
		 * The interleave is exactly what Helium's structured accesses
		 * do: vld2q splits the luma row into the even and odd samples
		 * the format wants for Y0 and Y1, and vst4q writes Y Cb Y Cr
		 * back out in one go, thirty-two pixels at a time.
		 */
		for (; x + 32U <= w; x += 32U) {
			uint8x16x2_t l = vld2q_u8(luma + x);
			uint8x16x4_t o;

			o.val[0] = l.val[0];
			o.val[1] = vld1q_u8(cb + x / 2U);
			o.val[2] = l.val[1];
			o.val[3] = vld1q_u8(cr + x / 2U);
			vst4q_u8(out + (size_t)x * 2U, o);
		}
#endif

		for (; x < w; x += 2U) {
			yuyv_pair(out + (size_t)x * 2U, luma[x], cb[x / 2U], luma[x + 1U],
				  cr[x / 2U]);
		}
	}
}

/*
 * Half the width and half the height, which is the only ratio split screen
 * settles at. Every second luma sample and every second chroma sample is
 * dropped, so no arithmetic is needed at all and only a quarter of the
 * picture is read: the split screen picture costs less than the full one.
 */
static void yuyv_2_1(uint8_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
		     uint16_t w, uint16_t h)
{
	const uint8_t *u = pic + (size_t)w * h;
	const uint8_t *v = u + (size_t)w * h / 4U;
	uint16_t pairs = r->w / 2U;

	for (uint16_t y = 0U; y < r->h; y++) {
		/* One output row per two picture rows, so chroma is not decimated */
		const uint8_t *luma = pic + (size_t)y * 2U * w;
		const uint8_t *cb = u + (size_t)y * (w / 2U);
		const uint8_t *cr = v + (size_t)y * (w / 2U);
		uint8_t *out = yuyv_row(dst, pitch, r->x, r->y + y);
		uint16_t k = 0U;

#ifdef HU_HAS_MVE
		/*
		 * A four way de-interleave is the stride the decimation wants:
		 * the pair at output column 2k comes from luma samples 4k and
		 * 4k+2, which vld4q hands over as its first and third result,
		 * and the chroma it shares is every second sample of each
		 * chroma row.
		 */
		for (; k + 16U <= pairs; k += 16U) {
			uint8x16x4_t l = vld4q_u8(luma + (size_t)k * 4U);
			uint8x16x2_t bc = vld2q_u8(cb + (size_t)k * 2U);
			uint8x16x2_t rc = vld2q_u8(cr + (size_t)k * 2U);
			uint8x16x4_t o;

			o.val[0] = l.val[0];
			o.val[1] = bc.val[0];
			o.val[2] = l.val[2];
			o.val[3] = rc.val[0];
			vst4q_u8(out + (size_t)k * 4U, o);
		}
#endif

		for (; k < pairs; k++) {
			yuyv_pair(out + (size_t)k * 4U, luma[(size_t)k * 4U], cb[(size_t)k * 2U],
				  luma[(size_t)k * 4U + 2U], cr[(size_t)k * 2U]);
		}
	}
}

/*
 * Any other ratio, which is only ever on screen while the layout is moving
 * between the two it settles at. Nearest neighbour keeps it to a fixed point
 * step and a load per sample, with no filtering to pay for.
 */
static void yuyv_nearest(uint8_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
			 uint16_t w, uint16_t h)
{
	const uint8_t *u = pic + (size_t)w * h;
	const uint8_t *v = u + (size_t)w * h / 4U;
	uint32_t x_step = ((uint32_t)w << 16) / r->w;
	uint32_t y_step = ((uint32_t)h << 16) / r->h;
	uint32_t y_acc = 0U;

	for (uint16_t y = 0U; y < r->h; y++) {
		uint16_t sy = (uint16_t)(y_acc >> 16);
		const uint8_t *luma = pic + (size_t)sy * w;
		const uint8_t *cb = u + (size_t)(sy / 2U) * (w / 2U);
		const uint8_t *cr = v + (size_t)(sy / 2U) * (w / 2U);
		uint8_t *out = yuyv_row(dst, pitch, r->x, r->y + y);
		uint32_t x_acc = 0U;

		for (uint16_t x = 0U; x < r->w; x += 2U) {
			uint16_t sx0 = (uint16_t)(x_acc >> 16);
			uint16_t sx1;

			x_acc += x_step;
			sx1 = (uint16_t)(x_acc >> 16);
			x_acc += x_step;

			yuyv_pair(out + (size_t)x * 2U, luma[sx0], cb[sx0 / 2U], luma[sx1],
				  cr[sx0 / 2U]);
		}

		y_acc += y_step;
	}
}

void aa_scale_i420_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
			uint16_t w, uint16_t h)
{
	if (r->w == w && r->h == h) {
		yuyv_1_1(dst, pitch, r, pic, w, h);
	} else if (r->w * 2U == w && r->h * 2U == h) {
		yuyv_2_1(dst, pitch, r, pic, w, h);
	} else {
		yuyv_nearest(dst, pitch, r, pic, w, h);
	}
}

#if !RGB_ROTATED
/* The whole picture, converted for a display that scans RGB */
static void rgb565_1_1(uint16_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
		       uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

#ifdef HU_HAS_MVE
	/* One chroma sample feeds two pixels: {0,0,1,1,2,2,3,3} */
	uint16x8_t dup = vshrq_n_u16(vidupq_n_u16(0, 1), 1);
#endif

	for (uint16_t y = 0U; y < h; y++) {
		const uint8_t *y_row = pic + (size_t)y * w;
		const uint8_t *cb_row = cb + (size_t)(y / 2U) * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)(y / 2U) * (w / 2U);
		uint16_t *out = dst + (size_t)(r->y + y) * pitch + r->x;
		uint16_t x = 0U;

#ifdef HU_HAS_MVE
		/*
		 * 298, 409, -208 and 516 each sit next to a power of two, and
		 * (256 * A + B) >> 8 is exactly A + (B >> 8), so every channel
		 * splits into a whole part and a remainder that stays inside
		 * int16. That keeps eight pixels to a vector where the plain
		 * products would need thirty-two bit lanes and only four.
		 */
		for (; x + 8U <= w; x += 8U) {
			int16x8_t c = vsubq_n_s16((int16x8_t)vldrbq_u16(y_row + x), 16);
			int16x8_t d = vsubq_n_s16((int16x8_t)vldrbq_gather_offset_u16(
							  cb_row + x / 2U, dup), 128);
			int16x8_t e = vsubq_n_s16((int16x8_t)vldrbq_gather_offset_u16(
							  cr_row + x / 2U, dup), 128);
			int16x8_t vr, vg, vb, acc;

			acc = vmlaq_n_s16(vmlaq_n_s16(vdupq_n_s16(128), c, 42), e, 153);
			vr = mve_clip255(vaddq_s16(vaddq_s16(c, e), vshrq_n_s16(acc, 8)));

			acc = vmlaq_n_s16(vmlaq_n_s16(vmlaq_n_s16(vdupq_n_s16(128), c, 42),
						      d, -100), e, 48);
			vg = mve_clip255(vaddq_s16(vsubq_s16(c, e), vshrq_n_s16(acc, 8)));

			acc = vmlaq_n_s16(vmlaq_n_s16(vdupq_n_s16(128), c, 42), d, 4);
			vb = mve_clip255(vaddq_s16(vaddq_s16(c, vshlq_n_s16(d, 1)),
						   vshrq_n_s16(acc, 8)));

			vstrhq_u16(out + x,
				   vorrq(vorrq(vshlq_n_u16(vandq_u16((uint16x8_t)vr,
								     vdupq_n_u16(0xF8)), 8),
					       vshlq_n_u16(vandq_u16((uint16x8_t)vg,
								     vdupq_n_u16(0xFC)), 3)),
					 vshrq_n_u16((uint16x8_t)vb, 3)));
		}
#endif

		for (; x < w; x++) {
			out[x] = yuv_to_rgb565(y_row[x], cb_row[x / 2U], cr_row[x / 2U]);
		}
	}
}

#else /* !RGB_ROTATED */

/*
 * The whole picture onto a turned surface. A row of the picture runs down a
 * column of the panel, so writing it a row at a time lands every pixel on a
 * line of memory of its own. Squares of the picture keep both sides local
 * instead: down a column of a square the pixels are neighbours on one line of
 * the panel, and the square's rows are few enough to stay cached while it is
 * walked. Two rows of the picture are two neighbouring pixels of that line,
 * so they are stored together as one word.
 *
 * The conversion is done by table, as for ARGB8888: a processor with no vector
 * unit spends more on the multiplies and the clamps than on the memory. Each
 * table holds one channel clamped and already in its bits of the RGB565 pixel.
 */
#define ROT_TILE 32U
#define ROT_BIAS 288
#define ROT_SPAN 1024
#define ROT_TERM (128 + (ROT_BIAS << 8))

static uint16_t rot_r[ROT_SPAN];
static uint16_t rot_g[ROT_SPAN];
static uint16_t rot_b[ROT_SPAN];
static int32_t rot_y[256];

static int rgb565_rot_tables(void)
{
	for (uint32_t i = 0U; i < ROT_SPAN; i++) {
		uint32_t v = (uint32_t)CLAMP((int32_t)i - ROT_BIAS, 0, 255);

		rot_r[i] = (uint16_t)((v & 0xF8U) << 8);
		rot_g[i] = (uint16_t)((v & 0xFCU) << 3);
		rot_b[i] = (uint16_t)(v >> 3);
	}

	for (int32_t y = 0; y < 256; y++) {
		rot_y[y] = 298 * (y - 16);
	}

	return 0;
}

SYS_INIT(rgb565_rot_tables, POST_KERNEL, 0);

static inline uint32_t rot_pixel(int32_t c, int32_t rt, int32_t gt, int32_t bt)
{
	return (uint32_t)rot_r[(c + rt) >> 8] | rot_g[(c + gt) >> 8] | rot_b[(c + bt) >> 8];
}

/* A pixel and the one below it in the picture, which are neighbours on the panel */
#if defined(CONFIG_SAMPLE_AA_HU_ROTATE_90)
#define ROT_PAIR(p, top, below) (*(uint32_t *)((p) - 1) = (below) | ((top) << 16))
#else
#define ROT_PAIR(p, top, below) (*(uint32_t *)(p) = (top) | ((below) << 16))
#endif

static void rgb565_1_1_rot(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			   const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

	for (uint16_t ty = 0U; ty < h; ty += ROT_TILE) {
		uint16_t y_end = MIN(ty + ROT_TILE, h);

		for (uint16_t tx = 0U; tx < w; tx += ROT_TILE) {
			uint16_t x_end = MIN(tx + ROT_TILE, w);

			for (uint16_t x = tx; x < x_end; x += 2U) {
				uint16_t *out0 = RGB_AT(dst, pitch, r->x + x, r->y + ty);
				uint16_t *out1 = out0 + RGB_STEP;

				for (uint16_t y = ty; y < y_end; y += 2U) {
					const uint8_t *l0 = pic + (size_t)y * w + x;
					const uint8_t *l1 = l0 + w;
					size_t ci = (size_t)(y / 2U) * (w / 2U) + x / 2U;
					int32_t d = (int32_t)cb[ci] - 128;
					int32_t e = (int32_t)cr[ci] - 128;
					int32_t rt = 409 * e + ROT_TERM;
					int32_t gt = -100 * d - 208 * e + ROT_TERM;
					int32_t bt = 516 * d + ROT_TERM;
					ptrdiff_t at = (ptrdiff_t)(y - ty) * RGB_DOWN;

					ROT_PAIR(out0 + at, rot_pixel(rot_y[l0[0]], rt, gt, bt),
						 rot_pixel(rot_y[l1[0]], rt, gt, bt));
					ROT_PAIR(out1 + at, rot_pixel(rot_y[l0[1]], rt, gt, bt),
						 rot_pixel(rot_y[l1[1]], rt, gt, bt));
				}
			}
		}
	}
}
#endif /* !RGB_ROTATED */

/*
 * Half in each direction. One output row per two picture rows means the chroma
 * row is used as it is, and one output pixel per two picture pixels means
 * every second chroma sample of it.
 */
static void rgb565_2_1(uint16_t *dst, uint16_t pitch, const struct aa_rect *r, const uint8_t *pic,
		       uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

	for (uint16_t y = 0U; y < r->h; y++) {
		const uint8_t *y_row = pic + (size_t)y * 2U * w;
		const uint8_t *cb_row = cb + (size_t)y * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)y * (w / 2U);
		uint16_t *out = RGB_AT(dst, pitch, r->x, r->y + y);

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x * RGB_STEP] =
				yuv_to_rgb565(y_row[(size_t)x * 2U], cb_row[x], cr_row[x]);
		}
	}
}

/* Any other ratio, for the frames the layout is being animated over */
static void rgb565_nearest(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			   const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;
	uint32_t x_step = ((uint32_t)w << 16) / r->w;
	uint32_t y_step = ((uint32_t)h << 16) / r->h;
	uint32_t y_acc = 0U;

	for (uint16_t y = 0U; y < r->h; y++) {
		uint16_t sy = (uint16_t)(y_acc >> 16);
		const uint8_t *y_row = pic + (size_t)sy * w;
		const uint8_t *cb_row = cb + (size_t)(sy / 2U) * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)(sy / 2U) * (w / 2U);
		uint16_t *out = RGB_AT(dst, pitch, r->x, r->y + y);
		uint32_t x_acc = 0U;

		for (uint16_t x = 0U; x < r->w; x++) {
			uint16_t sx = (uint16_t)(x_acc >> 16);

			out[x * RGB_STEP] =
				yuv_to_rgb565(y_row[sx], cb_row[sx / 2U], cr_row[sx / 2U]);
			x_acc += x_step;
		}

		y_acc += y_step;
	}
}

#ifdef CONFIG_SAMPLE_AA_HU_PPA
/*
 * The ESP32-P4's pixel processing accelerator converts a YUV picture to RGB565
 * and turns and scales it on its own. It only reads Espressif's packed 4:2:0,
 * in which every pair of luma samples is preceded by the pair's U sample on an
 * even line and by its V sample on an odd one, so the decoder's three planes
 * are interleaved into that first. That is a byte shuffle, done here eight
 * pixels to three words, where converting in software costs a multiply and a
 * clamp per channel and pixel.
 */
#define PPA_PACKED_SIZE ((size_t)STREAM_W * STREAM_H * 3U / 2U)
#define PPA_FB_SIZE     ((size_t)SCAN_W * SCAN_H * sizeof(uint16_t))

static uint8_t ppa_packed[PPA_PACKED_SIZE] AA_HU_BIG_BUF __aligned(128);
static ppa_client_handle_t ppa_client;

/*
 * The accelerator runs while the decoder works on the next picture. This is
 * available while it is neither reading ppa_packed nor writing a surface, and
 * ppa_dst is the surface it last wrote, until the cache is cleared of it.
 */
static K_SEM_DEFINE(ppa_idle, 1, 1);
static uint16_t *ppa_dst;

static bool ppa_done(ppa_client_handle_t client, ppa_event_data_t *event, void *user_data)
{
	ARG_UNUSED(client);
	ARG_UNUSED(event);
	ARG_UNUSED(user_data);

	k_sem_give(&ppa_idle);

	return false;
}

static int ppa_client_init(void)
{
	const ppa_client_config_t cfg = {
		.oper_type = PPA_OPERATION_SRM,
		.data_burst_length = PPA_DATA_BURST_LENGTH_128,
	};
	const ppa_event_callbacks_t cbs = {
		.on_trans_done = ppa_done,
	};

	if (ppa_register_client(&cfg, &ppa_client) != ESP_OK ||
	    ppa_client_register_event_callbacks(ppa_client, &cbs) != ESP_OK) {
		LOG_ERR("No accelerator client, converting in software");
		ppa_client = NULL;
	}

	return 0;
}

/*
 * Drop what the cache may have fetched of the surface while the accelerator
 * was writing it, so that the next drawing into it starts from memory.
 * Called with ppa_idle held.
 */
static void ppa_retire(void)
{
	if (ppa_dst != NULL) {
		sys_cache_data_invd_range(ppa_dst, PPA_FB_SIZE);
		ppa_dst = NULL;
	}
}

SYS_INIT(ppa_client_init, APPLICATION, 0);

static void ppa_pack_line(uint32_t *out, const uint8_t *luma, const uint8_t *chroma, uint16_t w)
{
	const uint32_t *l = (const uint32_t *)luma;
	const uint32_t *c = (const uint32_t *)chroma;

	for (uint16_t x = 0U; x < w; x += 8U) {
		uint32_t a = *l++;
		uint32_t b = *l++;
		uint32_t cw = *c++;

		*out++ = (cw & 0xFFU) | ((a & 0xFFFFU) << 8) | ((cw & 0xFF00U) << 16);
		*out++ = (a >> 16) | (cw & 0xFF0000U) | ((b & 0xFFU) << 24);
		*out++ = ((b >> 8) & 0xFFU) | ((cw >> 24) << 8) | (b & 0xFFFF0000U);
	}
}

/* Where the rectangle of the picture's frame lands on the panel */
static void ppa_out_rect(const struct aa_rect *r, uint32_t *x, uint32_t *y)
{
#if defined(CONFIG_SAMPLE_AA_HU_ROTATE_90)
	*x = SCAN_W - r->y - r->h;
	*y = r->x;
#elif defined(CONFIG_SAMPLE_AA_HU_ROTATE_270)
	*x = r->y;
	*y = SCAN_H - r->x - r->w;
#else
	*x = r->x;
	*y = r->y;
#endif
}

static int ppa_picture(uint16_t *dst, const struct aa_rect *r, const uint8_t *pic, uint16_t w,
		       uint16_t h)
{
	const uint8_t *u = pic + (size_t)w * h;
	const uint8_t *v = u + (size_t)w * h / 4U;
	ppa_srm_oper_config_t cfg = {
		.in = {
			.buffer = ppa_packed,
			.pic_w = w,
			.pic_h = h,
			.block_w = w,
			.block_h = h,
			.srm_cm = PPA_SRM_COLOR_MODE_YUV420,
			.yuv_range = PPA_COLOR_RANGE_LIMIT,
			.yuv_std = PPA_COLOR_CONV_STD_RGB_YUV_BT601,
		},
		.out = {
			.buffer = dst,
			.buffer_size = PPA_FB_SIZE,
			.pic_w = SCAN_W,
			.pic_h = SCAN_H,
			.srm_cm = PPA_SRM_COLOR_MODE_RGB565,
		},
#if defined(CONFIG_SAMPLE_AA_HU_ROTATE_90)
		.rotation_angle = PPA_SRM_ROTATION_ANGLE_270,
#elif defined(CONFIG_SAMPLE_AA_HU_ROTATE_270)
		.rotation_angle = PPA_SRM_ROTATION_ANGLE_90,
#else
		.rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
#endif
		.mode = PPA_TRANS_MODE_NON_BLOCKING,
	};
	esp_err_t err;

	/*
	 * The accelerator scales by a fraction with a few bits behind the
	 * point, so only the two sizes the layout settles at come out exactly
	 * the size of the rectangle. The ones in between are left to the
	 * processor.
	 */
	if (ppa_client == NULL || (size_t)w * h * 3U / 2U > sizeof(ppa_packed) || (w % 8U) != 0U) {
		return -ENOTSUP;
	}
	if (r->w == w && r->h == h) {
		cfg.scale_x = 1.0f;
		cfg.scale_y = 1.0f;
	} else if (r->w * 2U == w && r->h * 2U == h) {
		cfg.scale_x = 0.5f;
		cfg.scale_y = 0.5f;
	} else {
		return -ENOTSUP;
	}
	ppa_out_rect(r, &cfg.out.block_offset_x, &cfg.out.block_offset_y);

	/* The previous picture may still be being read out of ppa_packed */
	k_sem_take(&ppa_idle, K_FOREVER);
	ppa_retire();

	for (uint16_t y = 0U; y < h; y += 2U) {
		uint32_t *out = (uint32_t *)(ppa_packed + (size_t)y * w * 3U / 2U);
		size_t c = (size_t)(y / 2U) * (w / 2U);

		ppa_pack_line(out, pic + (size_t)y * w, u + c, w);
		ppa_pack_line(out + w * 3U / 8U, pic + (size_t)(y + 1U) * w, v + c, w);
	}
	sys_cache_data_flush_range(ppa_packed, sizeof(ppa_packed));

	/*
	 * Nothing the processor wrote may be written back over the picture
	 * once the accelerator has put it there, and nothing it cached before
	 * may be read in its place afterwards.
	 */
	sys_cache_data_flush_and_invd_range(dst, PPA_FB_SIZE);
	err = ppa_do_scale_rotate_mirror(ppa_client, &cfg);
	if (err != ESP_OK) {
		k_sem_give(&ppa_idle);
		LOG_WRN_ONCE("Accelerator failed (%d), converting in software", err);
		return -EIO;
	}
	ppa_dst = dst;

	return 0;
}
#endif /* CONFIG_SAMPLE_AA_HU_PPA */

void aa_scale_sync(void)
{
#ifdef CONFIG_SAMPLE_AA_HU_PPA
	k_sem_take(&ppa_idle, K_FOREVER);
	ppa_retire();
	k_sem_give(&ppa_idle);
#endif
}

void aa_scale_i420_rgb565(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint8_t *pic, uint16_t w, uint16_t h)
{
#ifdef CONFIG_SAMPLE_AA_HU_PPA
	if (ppa_picture(dst, r, pic, w, h) == 0) {
		return;
	}
#endif

	if (r->w == w && r->h == h) {
#if RGB_ROTATED
		/* Its word stores need the pair of panel pixels to start on a word */
		if ((r->y % 2U) == 0U) {
			rgb565_1_1_rot(dst, pitch, r, pic, w, h);
		} else {
			rgb565_nearest(dst, pitch, r, pic, w, h);
		}
#else
		rgb565_1_1(dst, pitch, r, pic, w, h);
#endif
	} else if (r->w * 2U == w && r->h * 2U == h) {
		rgb565_2_1(dst, pitch, r, pic, w, h);
	} else {
		rgb565_nearest(dst, pitch, r, pic, w, h);
	}
}

#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888

/*
 * The same conversion into a word per pixel, for a display that scans nothing
 * narrower. The alpha byte is what the format calls for rather than anything
 * the sample blends with: everything written here is opaque.
 *
 * Where the display is this one the processor is emulated, and instructions
 * rather than memory are what the frame rate is made of, so the conversion is
 * done by table. Each table holds one channel already clamped and already in
 * the byte it occupies, with the alpha riding in the red one, which turns a
 * multiply, an add, a shift and two compares per channel into a shift and a
 * load. The bias that makes every index positive is folded into the chroma
 * term, so it costs nothing per pixel.
 *
 * Thirteen kilobytes is the trade. A board whose display controller converts
 * during scanout never builds any of this.
 */
#define CLIP_BIAS 288
#define CLIP_SPAN 1024
#define CLIP_TERM (128 + (CLIP_BIAS << 8))

static uint32_t tab_r[CLIP_SPAN];
static uint32_t tab_g[CLIP_SPAN];
static uint32_t tab_b[CLIP_SPAN];
static int32_t tab_y[256];

static int argb8888_tables(void)
{
	for (uint32_t i = 0U; i < CLIP_SPAN; i++) {
		uint32_t v = (uint32_t)CLAMP((int32_t)i - CLIP_BIAS, 0, 255);

		tab_r[i] = 0xFF000000U | (v << 16);
		tab_g[i] = v << 8;
		tab_b[i] = v;
	}

	for (int32_t y = 0; y < 256; y++) {
		tab_y[y] = 298 * (y - 16);
	}

	return 0;
}

SYS_INIT(argb8888_tables, POST_KERNEL, 0);

static inline uint32_t argb8888(int32_t r, int32_t g, int32_t b)
{
	return 0xFF000000U | ((uint32_t)CLAMP(r, 0, 255) << 16) |
	       ((uint32_t)CLAMP(g, 0, 255) << 8) | (uint32_t)CLAMP(b, 0, 255);
}

/*
 * One pixel from its own luma and the chroma pair it shares. The three chroma
 * terms are what a caller converting a run of pixels hoists out of its loop.
 */
static inline uint32_t argb_pixel(int32_t c, int32_t rt, int32_t gt, int32_t bt)
{
	return tab_r[(c + rt) >> 8] | tab_g[(c + gt) >> 8] | tab_b[(c + bt) >> 8];
}

static inline uint32_t yuv_to_argb8888(int32_t y, int32_t cb, int32_t cr)
{
	int32_t d = cb - 128;
	int32_t e = cr - 128;

	return argb_pixel(tab_y[y], 409 * e + CLIP_TERM,
			  -100 * d - 208 * e + CLIP_TERM, 516 * d + CLIP_TERM);
}

/*
 * The whole picture. Two pixels side by side read the same chroma samples, so
 * the three products those samples enter into are computed once for the pair
 * and only the luma term changes between its two pixels. That is five
 * multiplies for two pixels where a pixel at a time needs six for one, which
 * is worth having on a processor without a vector unit to convert with.
 */
static void argb8888_1_1(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			 const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

	for (uint16_t y = 0U; y < h; y++) {
		const uint8_t *y_row = pic + (size_t)y * w;
		const uint8_t *cb_row = cb + (size_t)(y / 2U) * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)(y / 2U) * (w / 2U);
		uint32_t *out = dst + (size_t)(r->y + y) * pitch + r->x;
		uint16_t x = 0U;

		for (; (x + 1U) < w; x += 2U) {
			int32_t d = (int32_t)cb_row[x / 2U] - 128;
			int32_t e = (int32_t)cr_row[x / 2U] - 128;
			int32_t rt = 409 * e + CLIP_TERM;
			int32_t gt = -100 * d - 208 * e + CLIP_TERM;
			int32_t bt = 516 * d + CLIP_TERM;

			out[x] = argb_pixel(tab_y[y_row[x]], rt, gt, bt);
			out[x + 1U] = argb_pixel(tab_y[y_row[x + 1U]], rt, gt, bt);
		}

		if (x < w) {
			out[x] = yuv_to_argb8888(y_row[x], cb_row[x / 2U], cr_row[x / 2U]);
		}
	}
}

/* Half in each direction, the ratio split screen settles at */
static void argb8888_2_1(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			 const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

	for (uint16_t y = 0U; y < r->h; y++) {
		const uint8_t *y_row = pic + (size_t)y * 2U * w;
		const uint8_t *cb_row = cb + (size_t)y * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)y * (w / 2U);
		uint32_t *out = dst + (size_t)(r->y + y) * pitch + r->x;

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x] = yuv_to_argb8888(y_row[(size_t)x * 2U], cb_row[x], cr_row[x]);
		}
	}
}

/* Any other ratio, for the frames the layout is being animated over */
static void argb8888_nearest(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			     const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;
	uint32_t x_step = ((uint32_t)w << 16) / r->w;
	uint32_t y_step = ((uint32_t)h << 16) / r->h;
	uint32_t y_acc = 0U;

	for (uint16_t y = 0U; y < r->h; y++) {
		uint16_t sy = (uint16_t)(y_acc >> 16);
		const uint8_t *y_row = pic + (size_t)sy * w;
		const uint8_t *cb_row = cb + (size_t)(sy / 2U) * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)(sy / 2U) * (w / 2U);
		uint32_t *out = dst + (size_t)(r->y + y) * pitch + r->x;
		uint32_t x_acc = 0U;

		for (uint16_t x = 0U; x < r->w; x++) {
			uint16_t sx = (uint16_t)(x_acc >> 16);

			out[x] = yuv_to_argb8888(y_row[sx], cb_row[sx / 2U], cr_row[sx / 2U]);
			x_acc += x_step;
		}

		y_acc += y_step;
	}
}

void aa_scale_i420_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			    const uint8_t *pic, uint16_t w, uint16_t h)
{
	if (r->w == w && r->h == h) {
		argb8888_1_1(dst, pitch, r, pic, w, h);
	} else if (r->w * 2U == w && r->h * 2U == h) {
		argb8888_2_1(dst, pitch, r, pic, w, h);
	} else {
		argb8888_nearest(dst, pitch, r, pic, w, h);
	}
}

#endif /* CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888 */

void aa_scale_fill_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		uint32_t *out = (uint32_t *)yuyv_row(dst, pitch, r->x, r->y + y);

		for (uint16_t x = 0U; x < r->w / 2U; x++) {
			out[x] = sys_cpu_to_le32(YUYV_BLACK);
		}
	}
}

void aa_scale_fill_rgb565(uint16_t *dst, uint16_t pitch, const struct aa_rect *r)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		uint16_t *out = RGB_AT(dst, pitch, r->x, r->y + y);

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x * RGB_STEP] = 0U;
		}
	}
}

#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888
void aa_scale_fill_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		uint32_t *out = dst + (size_t)(r->y + y) * pitch + r->x;

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x] = 0xFF000000U;
		}
	}
}
#endif /* CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888 */

/* Expand a channel to eight bits by repeating its top bits into the gap */
static inline int32_t chan5(uint16_t px, unsigned int shift)
{
	uint32_t v = (px >> shift) & 0x1FU;

	return (int32_t)((v << 3U) | (v >> 2U));
}

static inline int32_t chan6(uint16_t px)
{
	uint32_t v = (px >> 5U) & 0x3FU;

	return (int32_t)((v << 2U) | (v >> 4U));
}

static inline uint8_t rgb_to_y(int32_t r, int32_t g, int32_t b)
{
	return (uint8_t)(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
}

void aa_scale_rgb565_yuyv(uint8_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint16_t *src, uint16_t src_pitch)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		const uint16_t *in = src + (size_t)y * src_pitch;
		uint8_t *out = yuyv_row(dst, pitch, r->x, r->y + y);

		for (uint16_t x = 0U; x < r->w; x += 2U) {
			int32_t r0 = chan5(in[x], 11U);
			int32_t g0 = chan6(in[x]);
			int32_t b0 = chan5(in[x], 0U);
			int32_t r1 = chan5(in[x + 1U], 11U);
			int32_t g1 = chan6(in[x + 1U]);
			int32_t b1 = chan5(in[x + 1U], 0U);
			/*
			 * The pair shares one pair of chroma samples. Taking
			 * them from the average of the two pixels rather than
			 * from the first keeps the colour of a one pixel wide
			 * edge, which GUI text is full of.
			 */
			int32_t ra = (r0 + r1) >> 1;
			int32_t ga = (g0 + g1) >> 1;
			int32_t ba = (b0 + b1) >> 1;
			uint8_t cb = (uint8_t)(((-38 * ra - 74 * ga + 112 * ba + 128) >> 8) + 128);
			uint8_t cr = (uint8_t)(((112 * ra - 94 * ga - 18 * ba + 128) >> 8) + 128);

			yuyv_pair(out + (size_t)x * 2U, rgb_to_y(r0, g0, b0), cb,
				  rgb_to_y(r1, g1, b1), cr);
		}
	}
}

void aa_scale_rgb565_copy(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint16_t *src, uint16_t src_pitch)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		const uint16_t *in = src + (size_t)y * src_pitch;
		uint16_t *out = RGB_AT(dst, pitch, r->x, r->y + y);

		if (!RGB_ROTATED) {
			memcpy(out, in, (size_t)r->w * sizeof(uint16_t));
			continue;
		}

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x * RGB_STEP] = in[x];
		}
	}
}

#ifdef CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888
void aa_scale_rgb565_argb8888(uint32_t *dst, uint16_t pitch, const struct aa_rect *r,
			      const uint16_t *src, uint16_t src_pitch)
{
	for (uint16_t y = 0U; y < r->h; y++) {
		const uint16_t *in = src + (size_t)y * src_pitch;
		uint32_t *out = dst + (size_t)(r->y + y) * pitch + r->x;

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x] = argb8888(chan5(in[x], 11U), chan6(in[x]), chan5(in[x], 0U));
		}
	}
}
#endif /* CONFIG_SAMPLE_AA_HU_DISPLAY_ARGB8888 */
