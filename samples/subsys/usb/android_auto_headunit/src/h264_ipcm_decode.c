/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "h264_ipcm_decode.h"

#include <errno.h>
#include <string.h>

#define NAL_TYPE_SLICE 1U
#define NAL_TYPE_IDR   5U
#define NAL_TYPE_SPS   7U
#define NAL_TYPE_PPS   8U

#define MB_TYPE_I_PCM   25U
#define MB_TYPE_P_I_PCM 30U

/* Bit reader over an emulation-prevention-free RBSP */
struct br {
	const uint8_t *p;
	size_t len;
	size_t bit;
};

static uint32_t br_u(struct br *b, unsigned int n)
{
	uint32_t v = 0;

	for (unsigned int i = 0; i < n; i++) {
		size_t byte = b->bit >> 3;
		uint8_t shift = 7U - (b->bit & 7U);
		uint32_t bit = (byte < b->len) ? ((b->p[byte] >> shift) & 1U) : 0U;

		v = (v << 1) | bit;
		b->bit++;
	}

	return v;
}

static uint32_t br_ue(struct br *b)
{
	unsigned int zeros = 0;

	while (br_u(b, 1) == 0U && (b->bit >> 3) <= b->len && zeros < 32U) {
		zeros++;
	}

	return (zeros == 0U) ? 0U : ((1U << zeros) - 1U + br_u(b, zeros));
}

static int32_t br_se(struct br *b)
{
	uint32_t k = br_ue(b);

	return (k & 1U) ? (int32_t)((k + 1U) / 2U) : -(int32_t)(k / 2U);
}

static void br_align(struct br *b)
{
	b->bit = (b->bit + 7U) & ~(size_t)7U;
}

/* Position of the rbsp stop bit, so the macroblock loop knows when a slice ends */
static size_t rbsp_stop_bit(const uint8_t *p, size_t len)
{
	size_t last = len;

	while (last > 0U && p[last - 1U] == 0U) {
		last--;
	}
	if (last == 0U) {
		return 0U;
	}

	for (uint8_t bit = 0; bit < 8U; bit++) {
		if ((p[last - 1U] >> bit) & 1U) {
			return ((last - 1U) * 8U) + (7U - bit);
		}
	}

	return 0U;
}

/* Copy a NAL payload into the scratch buffer, removing emulation prevention */
static int strip_ep(const uint8_t *nal, size_t len, uint8_t *out, size_t cap, size_t *out_len)
{
	size_t o = 0;
	unsigned int zeros = 0;

	for (size_t i = 0; i < len; i++) {
		uint8_t v = nal[i];

		if (zeros >= 2U && v == 0x03U) {
			zeros = 0U;
			continue;
		}
		if (o >= cap) {
			return -EMSGSIZE;
		}
		out[o++] = v;
		zeros = (v == 0U) ? zeros + 1U : 0U;
	}

	*out_len = o;

	return 0;
}

static uint16_t yuv_to_rgb565(int32_t y, int32_t cb, int32_t cr)
{
	int32_t c = y - 16;
	int32_t d = cb - 128;
	int32_t e = cr - 128;
	int32_t r = (298 * c + 409 * e + 128) >> 8;
	int32_t g = (298 * c - 100 * d - 208 * e + 128) >> 8;
	int32_t b = (298 * c + 516 * d + 128) >> 8;

	r = (r < 0) ? 0 : (r > 255 ? 255 : r);
	g = (g < 0) ? 0 : (g > 255 ? 255 : g);
	b = (b < 0) ? 0 : (b > 255 ? 255 : b);

	return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* Write one decoded I_PCM macroblock (384 samples) into the framebuffer */
static void put_mb(struct h264_ipcm_dec *dec, uint32_t mb, const uint8_t *pcm)
{
	const uint8_t *luma = pcm;
	const uint8_t *cb = pcm + 256;
	const uint8_t *cr = pcm + 320;
	uint32_t mx = (mb % dec->mbs_x) * 16U;
	uint32_t my = (mb / dec->mbs_x) * 16U;

	for (uint32_t y = 0; y < 16U; y++) {
		uint16_t *row = &dec->fb[(my + y) * dec->width + mx];

		for (uint32_t x = 0; x < 16U; x++) {
			uint32_t ci = (y / 2U) * 8U + (x / 2U);

			row[x] = yuv_to_rgb565(luma[y * 16U + x], cb[ci], cr[ci]);
		}
	}

	if (mx < dec->dirty_x0 * 16U) {
		dec->dirty_x0 = mx / 16U;
	}
	if (my < dec->dirty_y0 * 16U) {
		dec->dirty_y0 = my / 16U;
	}
	if ((mx / 16U) + 1U > dec->dirty_x1) {
		dec->dirty_x1 = (mx / 16U) + 1U;
	}
	if ((my / 16U) + 1U > dec->dirty_y1) {
		dec->dirty_y1 = (my / 16U) + 1U;
	}
}

/* Decode one slice NAL already stripped of emulation prevention */
static int decode_slice(struct h264_ipcm_dec *dec, const uint8_t *rbsp, size_t len, bool idr)
{
	struct br b = {.p = rbsp, .len = len, .bit = 0};
	size_t stop = rbsp_stop_bit(rbsp, len);
	uint32_t mb;

	mb = br_ue(&b);                 /* first_mb_in_slice */
	(void)br_ue(&b);                /* slice_type */
	(void)br_ue(&b);                /* pic_parameter_set_id */
	(void)br_u(&b, 8);              /* frame_num, log2_max_frame_num == 8 */

	if (idr) {
		(void)br_ue(&b);        /* idr_pic_id */
		(void)br_u(&b, 1);      /* no_output_of_prior_pics_flag */
		(void)br_u(&b, 1);      /* long_term_reference_flag */
	} else {
		(void)br_u(&b, 1);      /* num_ref_idx_active_override_flag */
		(void)br_u(&b, 1);      /* ref_pic_list_modification_flag_l0 */
		(void)br_u(&b, 1);      /* adaptive_ref_pic_marking_mode_flag */
	}
	(void)br_se(&b);                /* slice_qp_delta */
	(void)br_ue(&b);                /* disable_deblocking_filter_idc */

	while (b.bit < stop && mb < (uint32_t)dec->mbs_x * dec->mbs_y) {
		uint32_t mb_type;

		if (!idr) {
			uint32_t skip = br_ue(&b);

			mb += skip;             /* P_Skip macroblocks keep their pixels */
			if (b.bit >= stop) {
				break;
			}
		}

		mb_type = br_ue(&b);
		if ((idr && mb_type != MB_TYPE_I_PCM) || (!idr && mb_type != MB_TYPE_P_I_PCM)) {
			return -ENOTSUP;
		}

		br_align(&b);           /* pcm_alignment_zero_bit */
		if (((b.bit >> 3) + 384U) > len) {
			return -ENOTSUP;
		}
		put_mb(dec, mb, &rbsp[b.bit >> 3]);
		b.bit += 384U * 8U;
		mb++;
	}

	return 0;
}

int h264_ipcm_decode_init(struct h264_ipcm_dec *dec, uint16_t width, uint16_t height, uint16_t *fb,
			  uint8_t *rbsp, size_t rbsp_cap)
{
	if (dec == NULL || fb == NULL || rbsp == NULL || (width % 16U) != 0U ||
	    (height % 16U) != 0U || rbsp_cap < 512U) {
		return -EINVAL;
	}

	memset(dec, 0, sizeof(*dec));
	dec->width = width;
	dec->height = height;
	dec->mbs_x = width / 16U;
	dec->mbs_y = height / 16U;
	dec->fb = fb;
	dec->rbsp = rbsp;
	dec->rbsp_cap = rbsp_cap;

	return 0;
}

int h264_ipcm_decode_au(struct h264_ipcm_dec *dec, const uint8_t *au, size_t len)
{
	size_t i = 0;
	bool got_picture = false;
	bool idr_picture = false;

	dec->dirty_x0 = dec->mbs_x;
	dec->dirty_y0 = dec->mbs_y;
	dec->dirty_x1 = 0;
	dec->dirty_y1 = 0;

	while (i + 4U <= len) {
		size_t start;
		size_t nal_end;
		uint8_t nal_type;
		size_t rbsp_len;
		int ret;

		/* Find the next start code (00 00 01 or 00 00 00 01) */
		if (au[i] != 0U || au[i + 1U] != 0U) {
			i++;
			continue;
		}
		if (au[i + 2U] == 1U) {
			start = i + 3U;
		} else if (au[i + 2U] == 0U && au[i + 3U] == 1U) {
			start = i + 4U;
		} else {
			i++;
			continue;
		}

		nal_end = start;
		while (nal_end + 3U <= len &&
		       !(au[nal_end] == 0U && au[nal_end + 1U] == 0U && au[nal_end + 2U] <= 1U)) {
			nal_end++;
		}
		if (nal_end + 3U > len) {
			nal_end = len;
		}

		nal_type = au[start] & 0x1FU;
		i = nal_end;

		if (nal_type == NAL_TYPE_SPS || nal_type == NAL_TYPE_PPS) {
			continue;
		}
		if (nal_type != NAL_TYPE_SLICE && nal_type != NAL_TYPE_IDR) {
			continue;
		}

		ret = strip_ep(&au[start + 1U], nal_end - start - 1U, dec->rbsp, dec->rbsp_cap,
			       &rbsp_len);
		if (ret != 0) {
			return ret;
		}

		ret = decode_slice(dec, dec->rbsp, rbsp_len, nal_type == NAL_TYPE_IDR);
		if (ret != 0) {
			return ret;
		}

		got_picture = true;
		if (nal_type == NAL_TYPE_IDR) {
			idr_picture = true;
			dec->got_idr = true;
		}
	}

	if (!got_picture) {
		return -ENODATA;
	}

	return idr_picture ? 1 : 0;
}
