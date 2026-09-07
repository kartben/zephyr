/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_encoder.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "yuv.h"

LOG_MODULE_REGISTER(aa_encoder, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Frame production: the dirty macroblocks left by LVGL refreshes are captured
 * (clearing their dirty bits), the picture is measured with a null sink so
 * that the messenger knows the access unit size up front, then converted and
 * emitted. Macroblocks that change while a picture is emitted are re-marked
 * dirty by the display driver and go out with the next picture.
 */

static struct h264_ipcm_enc enc;
static uint8_t stage[CONFIG_SAMPLE_AA_H264_OUT_BUF_SIZE];
static bool need_idr = true;
static size_t sps_pps_len;

static struct {
	struct aa_video_stats pub;
	int64_t win_start;
	uint32_t win_frames;
	uint64_t win_bytes;
} stats;

int aa_encoder_init(void)
{
	const struct aa_display_geometry *g = aa_display_geometry();
	const struct h264_ipcm_cfg cfg = {
		.width_mbs = g->mbs_x,
		.height_mbs = g->mbs_y,
		.level_idc = CONFIG_SAMPLE_AA_H264_LEVEL_IDC,
		.mb_rows_per_slice = CONFIG_SAMPLE_AA_H264_SLICE_ROWS,
	};
	int ret;

	ret = h264_ipcm_init(&enc, &cfg, stage, sizeof(stage));
	if (ret != 0) {
		return ret;
	}

	ret = h264_ipcm_write_sps_pps(&enc, NULL, NULL);
	if (ret < 0) {
		return ret;
	}

	sps_pps_len = (size_t)ret;
	need_idr = true;
	memset(&stats, 0, sizeof(stats));
	stats.win_start = k_uptime_get();

	LOG_INF("H.264 %ux%u, %u macroblocks, %u rows per slice, SPS+PPS %zu bytes", g->width,
		g->height, AA_MBS, CONFIG_SAMPLE_AA_H264_SLICE_ROWS, sps_pps_len);

	return 0;
}

void aa_encoder_reset(void)
{
	h264_ipcm_reset(&enc);
	need_idr = true;
}

size_t aa_encoder_config_len(void)
{
	return sps_pps_len;
}

int aa_encoder_write_config(aa_video_sink_t sink, void *ctx)
{
	return h264_ipcm_write_sps_pps(&enc, sink, ctx);
}

void aa_encoder_convert_mb(uint32_t mb, uint8_t *pcm)
{
	const struct aa_display_geometry *g = aa_display_geometry();
	const uint8_t *fb = aa_display_framebuffer();
	uint32_t mb_px = 16U / g->scale;
	uint32_t px = (mb % g->mbs_x) * mb_px;
	uint32_t py = (mb / g->mbs_x) * mb_px;
	const uint8_t *src = &fb[(py * g->lv_width + px) * g->bpp];

	if (g->bpp == 2U) {
		if (g->scale == 1U) {
			yuv_mb_rgb565((const uint16_t *)src, g->lv_width, pcm);
		} else {
			yuv_mb_rgb565_x2((const uint16_t *)src, g->lv_width, pcm);
		}
	} else if (g->scale == 1U) {
		yuv_mb_l8(src, g->lv_width, pcm);
	} else {
		yuv_mb_l8_x2(src, g->lv_width, pcm);
	}
}

static int run_picture(const struct aa_frame_plan *plan, aa_video_sink_t sink, void *ctx)
{
	uint8_t pcm[H264_MB_BYTES];
	int ret;

	ret = h264_ipcm_begin_picture(&enc, plan->idr, sink, ctx);
	if (ret != 0) {
		return ret;
	}

	for (uint32_t mb = 0; mb < AA_MBS; mb++) {
		if (!atomic_test_bit(plan->mbs, mb)) {
			ret = h264_ipcm_skip_mb(&enc);
		} else if (sink == NULL) {
			ret = h264_ipcm_write_mb(&enc, NULL);
		} else {
			aa_encoder_convert_mb(mb, pcm);
			ret = h264_ipcm_write_mb(&enc, pcm);
		}

		if (ret != 0) {
			(void)h264_ipcm_end_picture(&enc);
			return ret;
		}
	}

	return h264_ipcm_end_picture(&enc);
}

int aa_encoder_prepare(bool force_idr, struct aa_frame_plan *plan)
{
	atomic_t *dirty = aa_display_dirty();
	uint32_t max_mbs = CONFIG_SAMPLE_AA_MAX_MBS_PER_FRAME;
	int ret;

	memset(plan, 0, sizeof(*plan));
	plan->idr = force_idr || need_idr;

	for (uint32_t mb = 0; mb < AA_MBS; mb++) {
		if (plan->idr) {
			atomic_clear_bit(dirty, mb);
			atomic_set_bit(plan->mbs, mb);
			plan->coded_mbs++;
		} else if (atomic_test_bit(dirty, mb)) {
			if (max_mbs == 0U || plan->coded_mbs < max_mbs) {
				atomic_clear_bit(dirty, mb);
				atomic_set_bit(plan->mbs, mb);
				plan->coded_mbs++;
			} else {
				plan->pending_mbs++;
			}
		}
	}

	if (plan->coded_mbs == 0U) {
		return -EAGAIN;
	}

	ret = run_picture(plan, NULL, NULL);
	if (ret < 0) {
		LOG_ERR("Measuring the picture failed (%d)", ret);
		aa_encoder_reset();
		return -EIO;
	}

	plan->au_len = (size_t)ret + (plan->idr ? sps_pps_len : 0U);

	return 0;
}

static void stats_update(bool idr, size_t bytes, uint32_t encode_us, uint32_t pending)
{
	int64_t now = k_uptime_get();
	int64_t elapsed = now - stats.win_start;

	stats.pub.frames++;
	if (idr) {
		stats.pub.idr_frames++;
	}
	stats.pub.bytes += bytes;
	stats.pub.last_encode_us = encode_us;
	stats.pub.pending_mbs = pending;
	stats.win_frames++;
	stats.win_bytes += bytes;

	if (elapsed >= 1000) {
		stats.pub.fps_x10 = (uint32_t)((stats.win_frames * 10000ULL) / (uint64_t)elapsed);
		stats.pub.kbit_s = (uint32_t)((stats.win_bytes * 8ULL) / (uint64_t)elapsed);
		stats.win_start = now;
		stats.win_frames = 0U;
		stats.win_bytes = 0U;
	}
}

int aa_encoder_emit(const struct aa_frame_plan *plan, aa_video_sink_t sink, void *ctx)
{
	uint32_t t0 = k_cycle_get_32();
	size_t total = 0;
	int ret;

	if (plan->idr) {
		ret = h264_ipcm_write_sps_pps(&enc, sink, ctx);
		if (ret < 0) {
			goto error;
		}
		total += (size_t)ret;
	}

	ret = run_picture(plan, sink, ctx);
	if (ret < 0) {
		goto error;
	}
	total += (size_t)ret;

	if (total != plan->au_len) {
		LOG_ERR("Access unit is %zu bytes, %zu measured", total, plan->au_len);
		ret = -EIO;
		goto error;
	}

	stats_update(plan->idr, total, k_cyc_to_us_floor32(k_cycle_get_32() - t0),
		     plan->pending_mbs);
	if (plan->idr) {
		need_idr = false;
	}

	return 0;

error:
	aa_encoder_reset();
	return ret;
}

void aa_encoder_get_stats(struct aa_video_stats *out)
{
	*out = stats.pub;
	if ((k_uptime_get() - stats.win_start) > 2000) {
		out->fps_x10 = 0U;
		out->kbit_s = 0U;
	}
}
