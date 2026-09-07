/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_ENCODER_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_ENCODER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/sys/atomic.h>

#include "aa_display.h"
#include "h264_ipcm.h"

/** Access unit byte sink, called with at most CONFIG_SAMPLE_AA_H264_OUT_BUF_SIZE bytes. */
typedef h264_sink_t aa_video_sink_t;

/** Macroblocks captured for one picture and the exact size it will produce. */
struct aa_frame_plan {
	bool idr;
	ATOMIC_DEFINE(mbs, AA_MBS);
	uint32_t coded_mbs;
	uint32_t pending_mbs;
	size_t au_len;
};

struct aa_video_stats {
	uint32_t frames;
	uint32_t idr_frames;
	uint64_t bytes;
	uint32_t fps_x10;
	uint32_t kbit_s;
	uint32_t last_encode_us;
	uint32_t pending_mbs;
};

/**
 * @brief Initialize the encoder for the configured stream geometry.
 *
 * @retval 0 Success.
 * @retval -EINVAL Invalid geometry.
 */
int aa_encoder_init(void);

/** @brief Force the next picture to be an IDR picture of the whole screen. */
void aa_encoder_reset(void);

/** @brief Size in bytes of the SPS and PPS NAL units. */
size_t aa_encoder_config_len(void);

/**
 * @brief Write the SPS and PPS NAL units.
 *
 * @return Bytes written or a negative errno from the sink.
 */
int aa_encoder_write_config(aa_video_sink_t sink, void *ctx);

/**
 * @brief Capture the dirty macroblocks and measure the resulting access unit.
 *
 * @param force_idr Code the whole screen as an IDR picture.
 * @param plan Filled with the captured macroblocks and the access unit size.
 *
 * @retval 0 A picture is ready to be emitted.
 * @retval -EAGAIN Nothing changed since the last picture.
 * @retval -EIO Encoder error.
 */
int aa_encoder_prepare(bool force_idr, struct aa_frame_plan *plan);

/**
 * @brief Encode the planned picture and push exactly plan->au_len bytes to the sink.
 *
 * On failure the sequence is reset and the next picture is an IDR picture.
 *
 * @retval 0 Success.
 * @retval -EIO The emitted size differs from the measured one.
 * @return Negative errno returned by the sink.
 */
int aa_encoder_emit(const struct aa_frame_plan *plan, aa_video_sink_t sink, void *ctx);

/** @brief Copy the current statistics. */
void aa_encoder_get_stats(struct aa_video_stats *stats);

/** @brief Convert stream macroblock mb from the framebuffer into I_PCM samples. */
void aa_encoder_convert_mb(uint32_t mb, uint8_t *pcm);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_ENCODER_H_ */
