/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "h264_ipcm.h"

#include <errno.h>
#include <string.h>

#define NAL_TYPE_SLICE 1U
#define NAL_TYPE_IDR   5U
#define NAL_TYPE_SPS   7U
#define NAL_TYPE_PPS   8U

#define NAL_REF_IDC_HIGH 3U
#define NAL_REF_IDC_P    2U

#define PROFILE_IDC_BASELINE 66U
/* constraint_set0_flag and constraint_set1_flag: Constrained Baseline */
#define CONSTRAINT_SET_FLAGS 0xC0U

#define SLICE_TYPE_P_ALL 5U
#define SLICE_TYPE_I_ALL 7U

#define MB_TYPE_I_PCM   25U
#define MB_TYPE_P_I_PCM (5U + MB_TYPE_I_PCM)

#define FRAME_NUM_BITS 8U

static int bw_flush(struct h264_bw *bw)
{
	int ret;

	if (bw->measure || bw->len == 0U) {
		bw->len = 0U;
		return 0;
	}

	ret = bw->sink(bw->ctx, bw->buf, bw->len);
	bw->len = 0U;
	if (ret != 0 && bw->err == 0) {
		bw->err = ret;
	}

	return ret;
}

static void bw_raw(struct h264_bw *bw, uint8_t b)
{
	bw->total++;
	if (bw->measure) {
		return;
	}

	if (bw->len == bw->cap) {
		(void)bw_flush(bw);
	}
	bw->buf[bw->len++] = b;
}

/* Payload byte with emulation prevention (7.4.1.1) */
static void bw_byte(struct h264_bw *bw, uint8_t b)
{
	if (bw->zeros == 2U && b <= 0x03U) {
		bw_raw(bw, 0x03U);
		bw->zeros = 0U;
	}

	bw_raw(bw, b);
	bw->zeros = (b == 0U) ? bw->zeros + 1U : 0U;
}

static void bw_bits(struct h264_bw *bw, unsigned int n, uint32_t v)
{
	while (n > 0U) {
		unsigned int room = 8U - bw->nbits;
		unsigned int take = (n < room) ? n : room;
		uint32_t chunk = (v >> (n - take)) & ((1U << take) - 1U);

		bw->acc = (bw->acc << take) | chunk;
		bw->nbits += take;
		n -= take;
		if (bw->nbits == 8U) {
			bw_byte(bw, (uint8_t)bw->acc);
			bw->acc = 0U;
			bw->nbits = 0U;
		}
	}
}

static void bw_ue(struct h264_bw *bw, uint32_t v)
{
	uint32_t x = v + 1U;
	unsigned int n = 0U;

	while ((x >> n) != 0U) {
		n++;
	}

	bw_bits(bw, n - 1U, 0U);
	bw_bits(bw, n, x);
}

static void bw_se(struct h264_bw *bw, int32_t v)
{
	uint32_t k = (v > 0) ? (2U * (uint32_t)v - 1U) : (2U * (uint32_t)(-v));

	bw_ue(bw, k);
}

static void bw_align_zero(struct h264_bw *bw)
{
	if (bw->nbits != 0U) {
		bw_bits(bw, 8U - bw->nbits, 0U);
	}
}

static void bw_trailing(struct h264_bw *bw)
{
	bw_bits(bw, 1U, 1U);
	bw_align_zero(bw);
}

static void bw_start_nal(struct h264_bw *bw, uint8_t ref_idc, uint8_t type)
{
	bw_raw(bw, 0x00U);
	bw_raw(bw, 0x00U);
	bw_raw(bw, 0x00U);
	bw_raw(bw, 0x01U);
	bw_raw(bw, (uint8_t)((ref_idc << 5) | type));
	bw->zeros = 0U;
}

static void bw_pcm(struct h264_bw *bw, const uint8_t *pcm)
{
	if (bw->measure) {
		bw->total += H264_MB_BYTES;
		bw->zeros = 0U;
		return;
	}

	for (size_t i = 0; i < H264_MB_BYTES; i++) {
		bw_byte(bw, pcm[i]);
	}
}

static void bw_begin(struct h264_bw *bw, h264_sink_t sink, void *ctx)
{
	bw->sink = sink;
	bw->ctx = ctx;
	bw->measure = (sink == NULL);
	bw->len = 0U;
	bw->acc = 0U;
	bw->nbits = 0U;
	bw->zeros = 0U;
	bw->err = 0;
	bw->total = 0U;
}

static void write_sps(struct h264_ipcm_enc *e)
{
	struct h264_bw *bw = &e->bw;

	bw_start_nal(bw, NAL_REF_IDC_HIGH, NAL_TYPE_SPS);
	bw_bits(bw, 8U, PROFILE_IDC_BASELINE);
	bw_bits(bw, 8U, CONSTRAINT_SET_FLAGS);
	bw_bits(bw, 8U, e->cfg.level_idc);
	bw_ue(bw, 0U); /* seq_parameter_set_id */
	bw_ue(bw, FRAME_NUM_BITS - 4U); /* log2_max_frame_num_minus4 */
	bw_ue(bw, 2U); /* pic_order_cnt_type: output order is decoding order */
	bw_ue(bw, 1U); /* max_num_ref_frames */
	bw_bits(bw, 1U, 0U); /* gaps_in_frame_num_value_allowed_flag */
	bw_ue(bw, (uint32_t)e->cfg.width_mbs - 1U); /* pic_width_in_mbs_minus1 */
	bw_ue(bw, (uint32_t)e->cfg.height_mbs - 1U); /* pic_height_in_map_units_minus1 */
	bw_bits(bw, 1U, 1U); /* frame_mbs_only_flag */
	bw_bits(bw, 1U, 1U); /* direct_8x8_inference_flag */
	bw_bits(bw, 1U, 0U); /* frame_cropping_flag */
	bw_bits(bw, 1U, 1U); /* vui_parameters_present_flag */

	/* VUI (E.1.1) */
	bw_bits(bw, 1U, 0U); /* aspect_ratio_info_present_flag */
	bw_bits(bw, 1U, 0U); /* overscan_info_present_flag */
	bw_bits(bw, 1U, 1U); /* video_signal_type_present_flag */
	bw_bits(bw, 3U, 5U); /* video_format: unspecified */
	bw_bits(bw, 1U, 0U); /* video_full_range_flag: limited range */
	bw_bits(bw, 1U, 1U); /* colour_description_present_flag */
	bw_bits(bw, 8U, 6U); /* colour_primaries: SMPTE 170M */
	bw_bits(bw, 8U, 6U); /* transfer_characteristics: SMPTE 170M */
	bw_bits(bw, 8U, 6U); /* matrix_coefficients: SMPTE 170M (BT.601) */
	bw_bits(bw, 1U, 0U); /* chroma_loc_info_present_flag */
	bw_bits(bw, 1U, 0U); /* timing_info_present_flag */
	bw_bits(bw, 1U, 0U); /* nal_hrd_parameters_present_flag */
	bw_bits(bw, 1U, 0U); /* vcl_hrd_parameters_present_flag */
	bw_bits(bw, 1U, 0U); /* pic_struct_present_flag */
	bw_bits(bw, 1U, 1U); /* bitstream_restriction_flag */
	bw_bits(bw, 1U, 1U); /* motion_vectors_over_pic_boundaries_flag */
	bw_ue(bw, 0U); /* max_bytes_per_pic_denom: no limit, pictures are all I_PCM */
	bw_ue(bw, 0U); /* max_bits_per_mb_denom: no limit */
	bw_ue(bw, 16U); /* log2_max_mv_length_horizontal */
	bw_ue(bw, 16U); /* log2_max_mv_length_vertical */
	bw_ue(bw, 0U); /* max_num_reorder_frames: display immediately */
	bw_ue(bw, 1U); /* max_dec_frame_buffering */
	bw_trailing(bw);
}

static void write_pps(struct h264_ipcm_enc *e)
{
	struct h264_bw *bw = &e->bw;

	bw_start_nal(bw, NAL_REF_IDC_HIGH, NAL_TYPE_PPS);
	bw_ue(bw, 0U); /* pic_parameter_set_id */
	bw_ue(bw, 0U); /* seq_parameter_set_id */
	bw_bits(bw, 1U, 0U); /* entropy_coding_mode_flag: CAVLC */
	bw_bits(bw, 1U, 0U); /* bottom_field_pic_order_in_frame_present_flag */
	bw_ue(bw, 0U); /* num_slice_groups_minus1 */
	bw_ue(bw, 0U); /* num_ref_idx_l0_default_active_minus1 */
	bw_ue(bw, 0U); /* num_ref_idx_l1_default_active_minus1 */
	bw_bits(bw, 1U, 0U); /* weighted_pred_flag */
	bw_bits(bw, 2U, 0U); /* weighted_bipred_idc */
	bw_se(bw, 0); /* pic_init_qp_minus26 */
	bw_se(bw, 0); /* pic_init_qs_minus26 */
	bw_se(bw, 0); /* chroma_qp_index_offset */
	bw_bits(bw, 1U, 1U); /* deblocking_filter_control_present_flag */
	bw_bits(bw, 1U, 0U); /* constrained_intra_pred_flag */
	bw_bits(bw, 1U, 0U); /* redundant_pic_cnt_present_flag */
	bw_trailing(bw);
}

static void open_slice(struct h264_ipcm_enc *e)
{
	struct h264_bw *bw = &e->bw;

	e->slice_end_mb = e->cur_mb + e->slice_mbs;
	if (e->slice_end_mb > e->total_mbs) {
		e->slice_end_mb = e->total_mbs;
	}
	e->skip_run = 0U;

	if (e->idr) {
		bw_start_nal(bw, NAL_REF_IDC_HIGH, NAL_TYPE_IDR);
	} else {
		bw_start_nal(bw, NAL_REF_IDC_P, NAL_TYPE_SLICE);
	}

	/* slice_header() (7.3.3) for frame_mbs_only_flag 1 and pic_order_cnt_type 2 */
	bw_ue(bw, e->cur_mb); /* first_mb_in_slice */
	bw_ue(bw, e->idr ? SLICE_TYPE_I_ALL : SLICE_TYPE_P_ALL); /* slice_type */
	bw_ue(bw, 0U); /* pic_parameter_set_id */
	bw_bits(bw, FRAME_NUM_BITS, e->frame_num);
	if (e->idr) {
		bw_ue(bw, e->idr_pic_id);
	} else {
		bw_bits(bw, 1U, 0U); /* num_ref_idx_active_override_flag */
		bw_bits(bw, 1U, 0U); /* ref_pic_list_modification_flag_l0 */
	}

	/* dec_ref_pic_marking() */
	if (e->idr) {
		bw_bits(bw, 1U, 0U); /* no_output_of_prior_pics_flag */
		bw_bits(bw, 1U, 0U); /* long_term_reference_flag */
	} else {
		bw_bits(bw, 1U, 0U); /* adaptive_ref_pic_marking_mode_flag: sliding window */
	}

	bw_se(bw, 0); /* slice_qp_delta */
	bw_ue(bw, 1U); /* disable_deblocking_filter_idc: samples stay untouched */
}

static void close_slice(struct h264_ipcm_enc *e)
{
	struct h264_bw *bw = &e->bw;

	if (!e->idr && e->skip_run > 0U) {
		bw_ue(bw, e->skip_run);
		e->skip_run = 0U;
	}

	bw_trailing(bw);
}

int h264_ipcm_init(struct h264_ipcm_enc *e, const struct h264_ipcm_cfg *cfg, uint8_t *stage,
		   size_t stage_len)
{
	if (e == NULL || cfg == NULL || stage == NULL || stage_len < 16U ||
	    cfg->width_mbs == 0U || cfg->height_mbs == 0U) {
		return -EINVAL;
	}

	memset(e, 0, sizeof(*e));
	e->cfg = *cfg;
	e->bw.buf = stage;
	e->bw.cap = stage_len;
	e->total_mbs = (uint32_t)cfg->width_mbs * cfg->height_mbs;
	if (cfg->mb_rows_per_slice == 0U) {
		e->slice_mbs = e->total_mbs;
	} else {
		e->slice_mbs = (uint32_t)cfg->mb_rows_per_slice * cfg->width_mbs;
		if (e->slice_mbs > e->total_mbs) {
			e->slice_mbs = e->total_mbs;
		}
	}

	return 0;
}

int h264_ipcm_write_sps_pps(struct h264_ipcm_enc *e, h264_sink_t sink, void *ctx)
{
	if (e->in_picture) {
		return -EBUSY;
	}

	bw_begin(&e->bw, sink, ctx);
	write_sps(e);
	write_pps(e);
	(void)bw_flush(&e->bw);

	return (e->bw.err != 0) ? e->bw.err : (int)e->bw.total;
}

int h264_ipcm_begin_picture(struct h264_ipcm_enc *e, bool idr, h264_sink_t sink, void *ctx)
{
	if (e->in_picture) {
		return -EBUSY;
	}

	bw_begin(&e->bw, sink, ctx);
	e->idr = idr;
	if (idr) {
		e->frame_num = 0U;
	}
	e->cur_mb = 0U;
	e->in_picture = true;
	open_slice(e);

	return 0;
}

static int next_mb(struct h264_ipcm_enc *e)
{
	if (!e->in_picture || e->cur_mb >= e->total_mbs) {
		return -EINVAL;
	}

	if (e->cur_mb == e->slice_end_mb) {
		close_slice(e);
		open_slice(e);
	}

	return 0;
}

int h264_ipcm_write_mb(struct h264_ipcm_enc *e, const uint8_t *pcm)
{
	struct h264_bw *bw = &e->bw;
	int ret;

	ret = next_mb(e);
	if (ret != 0) {
		return ret;
	}

	if (!bw->measure && pcm == NULL) {
		return -EINVAL;
	}

	if (e->idr) {
		bw_ue(bw, MB_TYPE_I_PCM);
	} else {
		bw_ue(bw, e->skip_run);
		e->skip_run = 0U;
		bw_ue(bw, MB_TYPE_P_I_PCM);
	}

	bw_align_zero(bw); /* pcm_alignment_zero_bit */
	bw_pcm(bw, pcm);
	e->cur_mb++;

	return bw->err;
}

int h264_ipcm_skip_mb(struct h264_ipcm_enc *e)
{
	int ret;

	if (e->idr) {
		return -EINVAL;
	}

	ret = next_mb(e);
	if (ret != 0) {
		return ret;
	}

	e->skip_run++;
	e->cur_mb++;

	return 0;
}

int h264_ipcm_end_picture(struct h264_ipcm_enc *e)
{
	struct h264_bw *bw = &e->bw;

	if (!e->in_picture) {
		return -EINVAL;
	}

	if (e->cur_mb != e->total_mbs) {
		e->in_picture = false;
		return -EINVAL;
	}

	close_slice(e);
	(void)bw_flush(bw);
	e->in_picture = false;

	if (!bw->measure) {
		if (e->idr) {
			e->frame_num = 1U;
			e->idr_pic_id++;
		} else {
			e->frame_num = (uint8_t)(e->frame_num + 1U);
		}
	}

	return (bw->err != 0) ? bw->err : (int)bw->total;
}

void h264_ipcm_reset(struct h264_ipcm_enc *e)
{
	e->in_picture = false;
	e->frame_num = 0U;
	e->idr_pic_id++;
}
