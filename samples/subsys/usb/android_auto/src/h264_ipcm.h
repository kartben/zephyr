/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_IPCM_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_IPCM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Minimal H.264 (ITU-T H.264 / ISO/IEC 14496-10) Constrained Baseline encoder
 * that codes every macroblock either as I_PCM (raw samples) or as P_Skip.
 * The output is lossless and needs no transform, prediction or CAVLC residual
 * coding; the price is bandwidth: a coded macroblock costs 384 bytes of samples
 * plus a few header bits.
 *
 * The encoder streams its output through a sink callback from a small staging
 * buffer and never needs to hold a whole picture. When the sink is NULL the
 * encoder runs in measure mode and only counts the bytes it would emit, which
 * gives the exact access unit size before anything is sent.
 */

/** Size of one I_PCM macroblock payload: 256 luma, 64 Cb and 64 Cr samples. */
#define H264_MB_BYTES 384U

/** Byte sink, called from the encoding thread with at most the staging size. */
typedef int (*h264_sink_t)(void *ctx, const uint8_t *data, size_t len);

struct h264_ipcm_cfg {
	uint16_t width_mbs;
	uint16_t height_mbs;
	uint8_t level_idc;
	/** Macroblock rows per slice, 0 for a single slice per picture. */
	uint16_t mb_rows_per_slice;
};

struct h264_bw {
	uint8_t *buf;
	size_t cap;
	size_t len;
	uint32_t acc;
	uint8_t nbits;
	uint8_t zeros;
	bool measure;
	h264_sink_t sink;
	void *ctx;
	int err;
	size_t total;
};

struct h264_ipcm_enc {
	struct h264_ipcm_cfg cfg;
	struct h264_bw bw;
	uint32_t total_mbs;
	uint32_t slice_mbs;
	uint32_t cur_mb;
	uint32_t slice_end_mb;
	uint32_t skip_run;
	uint16_t idr_pic_id;
	uint8_t frame_num;
	bool idr;
	bool in_picture;
};

/**
 * @brief Initialize an encoder instance.
 *
 * @param e Encoder state.
 * @param cfg Picture geometry and slicing configuration.
 * @param stage Staging buffer for output bytes.
 * @param stage_len Staging buffer size, at least 16 bytes.
 *
 * @retval 0 Success.
 * @retval -EINVAL Invalid configuration.
 */
int h264_ipcm_init(struct h264_ipcm_enc *e, const struct h264_ipcm_cfg *cfg, uint8_t *stage,
		   size_t stage_len);

/**
 * @brief Write the sequence and picture parameter sets as Annex B NAL units.
 *
 * @return Number of bytes emitted, or a negative errno from the sink.
 */
int h264_ipcm_write_sps_pps(struct h264_ipcm_enc *e, h264_sink_t sink, void *ctx);

/**
 * @brief Start a picture.
 *
 * @param e Encoder state.
 * @param idr true for an IDR picture (all macroblocks must be written), false
 *            for a P picture (macroblocks may be skipped).
 * @param sink Output sink, or NULL to measure the size only.
 * @param ctx Sink context.
 *
 * @retval 0 Success.
 * @retval -EBUSY A picture is already in progress.
 */
int h264_ipcm_begin_picture(struct h264_ipcm_enc *e, bool idr, h264_sink_t sink, void *ctx);

/**
 * @brief Code the next macroblock in raster order as I_PCM.
 *
 * @param e Encoder state.
 * @param pcm 384 samples in I_PCM order, may be NULL in measure mode. Every
 *            sample must be at least 16 so that no emulation prevention byte
 *            is ever inserted inside the payload, which keeps measured and
 *            emitted sizes identical.
 *
 * @retval 0 Success.
 * @retval -EINVAL No picture in progress or the picture is complete.
 */
int h264_ipcm_write_mb(struct h264_ipcm_enc *e, const uint8_t *pcm);

/**
 * @brief Skip the next macroblock (P pictures only).
 *
 * @retval 0 Success.
 * @retval -EINVAL Not in a P picture or the picture is complete.
 */
int h264_ipcm_skip_mb(struct h264_ipcm_enc *e);

/**
 * @brief Finish the picture, flush the staging buffer and advance frame_num.
 *
 * In measure mode the frame counters are left untouched so that a following
 * emit pass produces exactly the measured number of bytes.
 *
 * @return Number of bytes emitted for the picture, or a negative errno.
 */
int h264_ipcm_end_picture(struct h264_ipcm_enc *e);

/**
 * @brief Abort any picture in progress and restart the sequence.
 *
 * The next picture must be an IDR picture.
 */
void h264_ipcm_reset(struct h264_ipcm_enc *e);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_IPCM_H_ */
