/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "h264_dump.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>

#include "aa_display.h"
#include "aa_encoder.h"
#include "h264_dump_bottom.h"

LOG_MODULE_REGISTER(aa_dump, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Writes the emitted stream and, for every picture, the framebuffer converted
 * with the same macroblock converters as I420 so that a host decoder can be
 * checked for bit-exact output.
 */

#define YUV_SUFFIX ".yuv"

static int h264_fd = -1;
static int yuv_fd = -1;

static int write_all(int fd, const uint8_t *data, size_t len)
{
	while (len > 0U) {
		long n = aa_dump_bottom_write(fd, data, len);

		if (n <= 0) {
			return -EIO;
		}
		data += n;
		len -= (size_t)n;
	}

	return 0;
}

int aa_dump_open(void)
{
	char yuv_path[sizeof(CONFIG_SAMPLE_AA_H264_DUMP_PATH) + sizeof(YUV_SUFFIX)];

	h264_fd = aa_dump_bottom_open(CONFIG_SAMPLE_AA_H264_DUMP_PATH);
	if (h264_fd < 0) {
		LOG_ERR("Cannot create %s", CONFIG_SAMPLE_AA_H264_DUMP_PATH);
		return -EIO;
	}

	strcpy(yuv_path, CONFIG_SAMPLE_AA_H264_DUMP_PATH);
	strcat(yuv_path, YUV_SUFFIX);
	yuv_fd = aa_dump_bottom_open(yuv_path);
	if (yuv_fd < 0) {
		LOG_ERR("Cannot create %s", yuv_path);
		return -EIO;
	}

	LOG_INF("Dumping the stream to %s and the reference pictures to %s",
		CONFIG_SAMPLE_AA_H264_DUMP_PATH, yuv_path);

	return 0;
}

int aa_dump_h264(const uint8_t *data, size_t len)
{
	if (h264_fd < 0) {
		return 0;
	}

	return write_all(h264_fd, data, len);
}

/* Write one plane of the framebuffer as I420 rows, converting macroblock by macroblock */
static int dump_plane(uint32_t plane)
{
	const struct aa_display_geometry *g = aa_display_geometry();
	uint32_t rows = (plane == 0U) ? 16U : 8U;
	uint32_t line_len = (plane == 0U) ? 16U : 8U;
	uint32_t offset = (plane == 0U) ? 0U : 256U + (plane - 1U) * 64U;
	uint8_t pcm[H264_MB_BYTES];

	for (uint32_t my = 0; my < g->mbs_y; my++) {
		for (uint32_t row = 0; row < rows; row++) {
			for (uint32_t mx = 0; mx < g->mbs_x; mx++) {
				int ret;

				aa_encoder_convert_mb(my * g->mbs_x + mx, pcm);
				ret = write_all(yuv_fd, &pcm[offset + row * line_len], line_len);
				if (ret != 0) {
					return ret;
				}
			}
		}
	}

	return 0;
}

int aa_dump_yuv_frame(void)
{
	if (yuv_fd < 0) {
		return 0;
	}

	for (uint32_t plane = 0; plane < 3U; plane++) {
		int ret = dump_plane(plane);

		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}
