/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_H264_IPCM_DECODE_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_H264_IPCM_DECODE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Decoder for the I_PCM-only H.264 stream produced by the companion accessory
 * sample: every macroblock is either raw samples (I_PCM) or a P_Skip copy of
 * the previous picture, so decoding is the inverse of that encoder and needs no
 * transform, prediction or entropy decoding. It does NOT decode a real phone's
 * CAVLC stream; such macroblocks are reported as unsupported.
 *
 * The reconstructed picture is written into a caller-owned RGB565 framebuffer
 * so it can be shown on a display directly.
 */

struct h264_ipcm_dec {
	uint16_t width;
	uint16_t height;
	uint16_t mbs_x;
	uint16_t mbs_y;
	uint16_t *fb;          /* RGB565, width * height */
	uint8_t *rbsp;         /* scratch for one emulation-prevention-free NAL */
	size_t rbsp_cap;
	bool got_idr;
	uint32_t dirty_x0, dirty_y0, dirty_x1, dirty_y1; /* macroblock bounds updated */
};

/**
 * @brief Initialize the decoder for a stream resolution.
 *
 * @param dec Decoder state.
 * @param width Stream width in pixels (multiple of 16).
 * @param height Stream height in pixels (multiple of 16).
 * @param fb RGB565 framebuffer of width * height pixels.
 * @param rbsp Scratch buffer for one NAL unit without emulation prevention.
 * @param rbsp_cap Scratch buffer size; NAL units larger than this are rejected,
 *                 so the encoder must slice the picture (the default).
 *
 * @retval 0 Success.
 * @retval -EINVAL Invalid geometry.
 */
int h264_ipcm_decode_init(struct h264_ipcm_dec *dec, uint16_t width, uint16_t height, uint16_t *fb,
			  uint8_t *rbsp, size_t rbsp_cap);

/**
 * @brief Decode one Annex B access unit into the framebuffer.
 *
 * The dirty_* bounds report the macroblock rectangle that changed.
 *
 * @retval 1 An IDR picture was decoded (the whole framebuffer is valid).
 * @retval 0 A picture was decoded (only the dirty rectangle changed).
 * @retval -ENODATA No picture was present, e.g. parameter sets only.
 * @retval -ENOTSUP A macroblock the decoder cannot handle was found.
 * @retval -EMSGSIZE A NAL unit did not fit the scratch buffer.
 */
int h264_ipcm_decode_au(struct h264_ipcm_dec *dec, const uint8_t *au, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_H264_IPCM_DECODE_H_ */
