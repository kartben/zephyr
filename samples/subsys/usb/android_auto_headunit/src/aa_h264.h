/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>

/**
 * @brief Set up the baseline decoder.
 *
 * Decoded pictures are handed to the screen, which composes them with
 * whatever else the display is showing.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The decoder could not be allocated.
 */
int aa_h264_init(void);

/**
 * @brief Discard the decoder's state and start a new stream.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The decoder could not be allocated.
 */
int aa_h264_reset(void);

/**
 * @brief Decode one access unit.
 *
 * @retval 1 A picture was decoded and shown.
 * @retval 0 The access unit carried no complete picture.
 * @retval -EINVAL The stream could not be decoded.
 */
int aa_h264_decode_au(const uint8_t *au, size_t len);

/**
 * @brief Keep a thread that decodes on the first CPU.
 *
 * Vector kernels of the decoder keep intermediate results in registers that
 * a thread switch does not save, which is only harmless as long as the thread
 * comes back on the CPU it left. Call before the thread is started.
 *
 * @param thread The thread that will call aa_h264_decode_au().
 */
static inline void aa_h264_pin(k_tid_t thread)
{
#ifdef CONFIG_SCHED_CPU_MASK
	(void)k_thread_cpu_pin(thread, 0);
#else
	ARG_UNUSED(thread);
#endif
}

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_H264_H_ */
