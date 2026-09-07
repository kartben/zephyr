/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "yuv.h"

#include <string.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>

/*
 * BT.601 limited range, 8-bit fixed point:
 *   Y  = ( 66 R + 129 G +  25 B + 128) / 256 + 16
 *   Cb = (-38 R -  74 G + 112 B + 128) / 256 + 128
 *   Cr = (112 R -  94 G -  18 B + 128) / 256 + 128
 * The per-component products are tabulated per 5/6-bit RGB565 field.
 */
#define Y_OFFSET  4224   /* 128 + 16 * 256 */
#define C_OFFSET  32896  /* 128 + 128 * 256 */
#define C_OFFSET4 131584 /* 512 + 128 * 1024, for a sum of four samples */

#define R5_TO_8(v) ((((v) << 3) | ((v) >> 2)) & 0xFF)
#define G6_TO_8(v) ((((v) << 2) | ((v) >> 4)) & 0xFF)

#define LUT_Y_R(i, _)  (66 * R5_TO_8(i))
#define LUT_Y_G(i, _)  (129 * G6_TO_8(i))
#define LUT_Y_B(i, _)  (25 * R5_TO_8(i))
#define LUT_CB_R(i, _) (-38 * R5_TO_8(i))
#define LUT_CB_G(i, _) (-74 * G6_TO_8(i))
#define LUT_CB_B(i, _) (112 * R5_TO_8(i))
#define LUT_CR_R(i, _) (112 * R5_TO_8(i))
#define LUT_CR_G(i, _) (-94 * G6_TO_8(i))
#define LUT_CR_B(i, _) (-18 * R5_TO_8(i))
#define LUT_L8_Y(i, _) (16 + (((i) * 219) + 127) / 255)

static const int32_t y_r[32] = {LISTIFY(32, LUT_Y_R, (,))};
static const int32_t y_g[64] = {LISTIFY(64, LUT_Y_G, (,))};
static const int32_t y_b[32] = {LISTIFY(32, LUT_Y_B, (,))};
static const int32_t cb_r[32] = {LISTIFY(32, LUT_CB_R, (,))};
static const int32_t cb_g[64] = {LISTIFY(64, LUT_CB_G, (,))};
static const int32_t cb_b[32] = {LISTIFY(32, LUT_CB_B, (,))};
static const int32_t cr_r[32] = {LISTIFY(32, LUT_CR_R, (,))};
static const int32_t cr_g[64] = {LISTIFY(64, LUT_CR_G, (,))};
static const int32_t cr_b[32] = {LISTIFY(32, LUT_CR_B, (,))};
static const uint8_t l8_y[256] = {LISTIFY(256, LUT_L8_Y, (,))};

#define CHROMA_NEUTRAL 128U

struct yuv_terms {
	int32_t y;
	int32_t cb;
	int32_t cr;
};

static inline void rgb565_terms(uint16_t px, struct yuv_terms *t)
{
	uint32_t r = (px >> 11) & 0x1FU;
	uint32_t g = (px >> 5) & 0x3FU;
	uint32_t b = px & 0x1FU;

	t->y = y_r[r] + y_g[g] + y_b[b];
	t->cb = cb_r[r] + cb_g[g] + cb_b[b];
	t->cr = cr_r[r] + cr_g[g] + cr_b[b];
}

void yuv_mb_rgb565(const uint16_t *src, size_t stride_px, uint8_t *pcm)
{
	uint8_t *luma = pcm;
	uint8_t *cb = pcm + 256;
	uint8_t *cr = pcm + 320;

	for (uint32_t row = 0; row < 16U; row += 2U) {
		const uint16_t *r0 = src + row * stride_px;
		const uint16_t *r1 = r0 + stride_px;

		for (uint32_t col = 0; col < 16U; col += 2U) {
			struct yuv_terms t00, t01, t10, t11;

			rgb565_terms(r0[col], &t00);
			rgb565_terms(r0[col + 1U], &t01);
			rgb565_terms(r1[col], &t10);
			rgb565_terms(r1[col + 1U], &t11);

			luma[row * 16U + col] = (uint8_t)((t00.y + Y_OFFSET) >> 8);
			luma[row * 16U + col + 1U] = (uint8_t)((t01.y + Y_OFFSET) >> 8);
			luma[(row + 1U) * 16U + col] = (uint8_t)((t10.y + Y_OFFSET) >> 8);
			luma[(row + 1U) * 16U + col + 1U] = (uint8_t)((t11.y + Y_OFFSET) >> 8);

			cb[(row / 2U) * 8U + col / 2U] =
				(uint8_t)((t00.cb + t01.cb + t10.cb + t11.cb + C_OFFSET4) >> 10);
			cr[(row / 2U) * 8U + col / 2U] =
				(uint8_t)((t00.cr + t01.cr + t10.cr + t11.cr + C_OFFSET4) >> 10);
		}
	}
}

void yuv_mb_rgb565_x2(const uint16_t *src, size_t stride_px, uint8_t *pcm)
{
	uint8_t *luma = pcm;
	uint8_t *cb = pcm + 256;
	uint8_t *cr = pcm + 320;

	for (uint32_t sy = 0; sy < 8U; sy++) {
		const uint16_t *r = src + sy * stride_px;
		uint8_t *l0 = luma + (2U * sy) * 16U;
		uint8_t *l1 = l0 + 16U;

		for (uint32_t sx = 0; sx < 8U; sx++) {
			struct yuv_terms t;
			uint8_t yv;

			rgb565_terms(r[sx], &t);
			yv = (uint8_t)((t.y + Y_OFFSET) >> 8);
			l0[2U * sx] = yv;
			l0[2U * sx + 1U] = yv;
			l1[2U * sx] = yv;
			l1[2U * sx + 1U] = yv;
			cb[sy * 8U + sx] = (uint8_t)((t.cb + C_OFFSET) >> 8);
			cr[sy * 8U + sx] = (uint8_t)((t.cr + C_OFFSET) >> 8);
		}
	}
}

void yuv_mb_l8(const uint8_t *src, size_t stride_px, uint8_t *pcm)
{
	for (uint32_t row = 0; row < 16U; row++) {
		const uint8_t *r = src + row * stride_px;

		for (uint32_t col = 0; col < 16U; col++) {
			pcm[row * 16U + col] = l8_y[r[col]];
		}
	}

	memset(pcm + 256, CHROMA_NEUTRAL, 128);
}

void yuv_mb_l8_x2(const uint8_t *src, size_t stride_px, uint8_t *pcm)
{
	for (uint32_t sy = 0; sy < 8U; sy++) {
		const uint8_t *r = src + sy * stride_px;
		uint8_t *l0 = pcm + (2U * sy) * 16U;
		uint8_t *l1 = l0 + 16U;

		for (uint32_t sx = 0; sx < 8U; sx++) {
			uint8_t yv = l8_y[r[sx]];

			l0[2U * sx] = yv;
			l0[2U * sx + 1U] = yv;
			l1[2U * sx] = yv;
			l1[2U * sx + 1U] = yv;
		}
	}

	memset(pcm + 256, CHROMA_NEUTRAL, 128);
}
