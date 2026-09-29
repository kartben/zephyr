/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/mpipe/mpipe.h>
#include <zephyr/mpipe/base/mpipe_caps_filter.h>
#include <zephyr/mpipe/disp/mpipe_disp_sink.h>
#include <zephyr/mpipe/utils/mpipe_player.h>
#include <zephyr/mpipe/vid/mpipe_vid_src.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/video/controls.h>

#include "espdet_overlay.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

enum {
	PIPE_ID,
	VID_SRC_ID,
	CAPS_FILTER_ID,
	ESPDET_ID,
	DISP_SINK_ID,
};

static struct mpipe pipe;
static struct mpipe_vid_src vid_src;
static struct mpipe_caps_filter caps_filter;
static struct espdet_overlay espdet;
static struct mpipe_disp_sink disp_sink;
static struct mpipe_player player;

int main(void)
{
	struct mpipe_structure caps;
	int ret;

	ret = mpipe_pipeline_init(&pipe, PIPE_ID);
	if (ret == 0) {
		ret = mpipe_vid_src_init(&vid_src, VID_SRC_ID);
	}
	if (ret == 0) {
		ret = mpipe_caps_filter_init(&caps_filter, CAPS_FILTER_ID);
	}
	if (ret == 0) {
		ret = espdet_overlay_init(&espdet, ESPDET_ID);
	}
	if (ret == 0) {
		ret = mpipe_disp_sink_init(&disp_sink, DISP_SINK_ID);
	}
	if (ret != 0) {
		LOG_ERR("Failed to initialize the elements (%d)", ret);
		return 0;
	}

	/* clang-format off */
	ret = mpipe_object_set_properties((struct mpipe_object *)&vid_src,
		IF_ENABLED(CONFIG_VIDEO_CTRL_HFLIP, (VIDEO_CID_HFLIP, 1,))
		IF_ENABLED(CONFIG_VIDEO_CTRL_VFLIP, (VIDEO_CID_VFLIP, 1,))
		MPIPE_PROP_LIST_END);
	/* clang-format on */
	if (ret != 0) {
		LOG_ERR("Cannot set the camera flip controls (%d)", ret);
		return 0;
	}

	/* The camera frame the detector and the display both work on */
	ret = mpipe_structure_init_fields(
		&caps, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT, MPIPE_TYPE_UINT,
		VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT), MPIPE_CAPS_IMAGE_WIDTH,
		MPIPE_TYPE_UINT, CONFIG_VIDEO_FRAME_WIDTH, MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT,
		CONFIG_VIDEO_FRAME_HEIGHT, MPIPE_CAPS_END);
	if (ret == 0) {
		ret = mpipe_object_set_properties((struct mpipe_object *)&caps_filter,
						  MPIPE_PROP_BASE_CAPS_FILTER_CAPS, &caps,
						  MPIPE_PROP_LIST_END);
	}
	if (ret != 0) {
		LOG_ERR("Failed to set the frame format (%d)", ret);
		return 0;
	}

	ret = mpipe_bin_add((struct mpipe_bin *)&pipe, (struct mpipe_element *)&vid_src,
			    (struct mpipe_element *)&caps_filter, (struct mpipe_element *)&espdet,
			    (struct mpipe_element *)&disp_sink, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to add the elements (%d)", ret);
		return 0;
	}

	ret = mpipe_element_link(
		(struct mpipe_element *)&vid_src, (struct mpipe_element *)&caps_filter,
		(struct mpipe_element *)&espdet, (struct mpipe_element *)&disp_sink, NULL);
	if (ret != 0) {
		LOG_ERR("Failed to link the elements (%d)", ret);
		return 0;
	}

	ret = mpipe_player_init(&player, &pipe);
	if (ret != 0) {
		LOG_ERR("Failed to initialize the player (%d)", ret);
		return 0;
	}

	LOG_INF("camera -> espdet overlay -> display");

	(void)mpipe_player_play(&player);
	(void)mpipe_player_wait_quit(&player);
	(void)mpipe_player_deinit(&player);

	return 0;
}
