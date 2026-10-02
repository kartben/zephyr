/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ESPDET_OVERLAY_H_
#define ESPDET_OVERLAY_H_

#include <zephyr/kernel.h>
#include <zephyr/video/video.h>
#include <zephyr/mpipe/mpipe_buffer.h>
#include <zephyr/mpipe/mpipe_transform.h>

#include "detector.h"

/**
 * In-place transform that hands frames to an object detector running on its
 * own thread and draws the latest detections on every frame going through.
 *
 * The stream never waits for the detector: a frame is copied for detection
 * only when the previous detection is done, so the video runs at the camera
 * rate while the boxes refresh at the detector rate.
 */
struct espdet_overlay {
	/** Base transform, must be first */
	struct mpipe_transform transform;

	/* Negotiated frame layout */
	uint16_t width;
	uint16_t height;
	uint32_t pitch;
	bool big_endian;

	/* Downstream buffer pool proposal, handed over to upstream */
	struct mpipe_buffer_pool *down_pool;
	struct mpipe_buffer_pool_config down_pool_cfg;

	/* Copy of the frame being analyzed, a buffer of the video buffer pool */
	struct video_buffer *snapshot;
	atomic_t busy;
	struct k_sem frame_ready;

	/* Latest detections */
	struct k_mutex lock;
	struct detector_box boxes[CONFIG_ESPDET_MAX_BOXES];
	size_t box_count;
	uint32_t infer_ms;
	uint32_t infer_count;
	int init_err;

	/* Frame rate of the video stream */
	uint32_t frame_count;
	int64_t fps_start;
	uint32_t fps_x10;
};

int espdet_overlay_init(struct espdet_overlay *self, uint8_t id);

#endif /* ESPDET_OVERLAY_H_ */
