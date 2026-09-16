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

#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

/* One pixel pair of a packed YUYV row, which the alignment makes a whole word */
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

/* Interleaving the three planes is all the packed format needs */
void aa_scale_i420_yuyv(uint8_t *dst, const uint8_t *pic, uint16_t w, uint16_t h)
{
	const uint8_t *u = pic + (size_t)w * h;
	const uint8_t *v = u + (size_t)w * h / 4U;

	for (uint16_t y = 0U; y < h; y++) {
		const uint8_t *luma = pic + (size_t)y * w;
		const uint8_t *cb = u + (size_t)(y / 2U) * (w / 2U);
		const uint8_t *cr = v + (size_t)(y / 2U) * (w / 2U);
		uint8_t *out = dst + (size_t)y * w * 2U;
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

void aa_scale_i420_rgb565(uint16_t *dst, uint16_t dst_w, uint16_t dst_h, const uint8_t *pic,
			  uint16_t w, uint16_t h)
{
	const uint8_t *cb = pic + (size_t)w * h;
	const uint8_t *cr = cb + (size_t)w * h / 4U;

#ifdef HU_HAS_MVE
	/* One chroma sample feeds two pixels: {0,0,1,1,2,2,3,3} */
	uint16x8_t dup = vshrq_n_u16(vidupq_n_u16(0, 1), 1);
#endif

	uint16_t rows = MIN(h, dst_h);
	uint16_t cols = MIN(w, dst_w);

	for (uint16_t y = 0U; y < rows; y++) {
		const uint8_t *y_row = pic + (size_t)y * w;
		const uint8_t *cb_row = cb + (size_t)(y / 2U) * (w / 2U);
		const uint8_t *cr_row = cr + (size_t)(y / 2U) * (w / 2U);
		uint16_t *out = dst + (size_t)y * dst_w;
		uint16_t x = 0U;

#ifdef HU_HAS_MVE
		/*
		 * 298, 409, -208 and 516 each sit next to a power of two, and
		 * (256 * A + B) >> 8 is exactly A + (B >> 8), so every channel
		 * splits into a whole part and a remainder that stays inside
		 * int16. That keeps eight pixels to a vector where the plain
		 * products would need thirty-two bit lanes and only four.
		 */
		for (; x + 8U <= cols; x += 8U) {
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

		for (; x < cols; x++) {
			out[x] = yuv_to_rgb565(y_row[x], cb_row[x / 2U], cr_row[x / 2U]);
		}
	}
}
