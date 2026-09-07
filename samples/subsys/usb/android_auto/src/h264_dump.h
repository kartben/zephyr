/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_SAMPLE_AA_H264_DUMP

/** @brief Open the H.264 and reference YUV dump files on the host. */
int aa_dump_open(void);

/** @brief Append access unit bytes to the H.264 dump. */
int aa_dump_h264(const uint8_t *data, size_t len);

/** @brief Append the current framebuffer as one I420 picture to the YUV dump. */
int aa_dump_yuv_frame(void);

#else

static inline int aa_dump_open(void)
{
	return 0;
}

static inline int aa_dump_h264(const uint8_t *data, size_t len)
{
	return 0;
}

static inline int aa_dump_yuv_frame(void)
{
	return 0;
}

#endif /* CONFIG_SAMPLE_AA_H264_DUMP */

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_H_ */
