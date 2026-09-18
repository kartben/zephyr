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

#include <string.h>

#include <zephyr/init.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

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
		uint16_t *out = dst + (size_t)(r->y + y) * pitch + r->x;

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x] = yuv_to_rgb565(y_row[(size_t)x * 2U], cb_row[x], cr_row[x]);
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
		uint16_t *out = dst + (size_t)(r->y + y) * pitch + r->x;
		uint32_t x_acc = 0U;

		for (uint16_t x = 0U; x < r->w; x++) {
			uint16_t sx = (uint16_t)(x_acc >> 16);

			out[x] = yuv_to_rgb565(y_row[sx], cb_row[sx / 2U], cr_row[sx / 2U]);
			x_acc += x_step;
		}

		y_acc += y_step;
	}
}

void aa_scale_i420_rgb565(uint16_t *dst, uint16_t pitch, const struct aa_rect *r,
			  const uint8_t *pic, uint16_t w, uint16_t h)
{
	if (r->w == w && r->h == h) {
		rgb565_1_1(dst, pitch, r, pic, w, h);
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
		uint16_t *out = dst + (size_t)(r->y + y) * pitch + r->x;

		for (uint16_t x = 0U; x < r->w; x++) {
			out[x] = 0U;
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
		memcpy(dst + (size_t)(r->y + y) * pitch + r->x, src + (size_t)y * src_pitch,
		       (size_t)r->w * sizeof(uint16_t));
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
