/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_GUI_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_GUI_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/toolchain.h>

#ifdef CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN

/**
 * @brief Start the head unit's own GUI.
 *
 * @retval 0 Success.
 * @retval -ENOMEM The GUI could not be built.
 */
int aa_gui_init(void);

/**
 * @brief Copy the GUI into a packed YUYV display buffer.
 *
 * Only what has changed since this buffer was last written is converted,
 * unless the layout has moved the GUI since, in which case all of it is.
 *
 * @param dst   Start of the surface.
 * @param pitch Surface width in pixels.
 * @param idx   Which of the display buffers @p dst is.
 */
void aa_gui_apply_yuyv(uint8_t *dst, uint16_t pitch, unsigned int idx);

/** @brief Copy the GUI into an RGB565 display buffer. */
void aa_gui_apply_rgb565(uint16_t *dst, uint16_t pitch, unsigned int idx);

/** @brief Copy the GUI into an ARGB8888 display buffer. */
void aa_gui_apply_argb8888(uint32_t *dst, uint16_t pitch, unsigned int idx);

/** @brief Whether any display buffer is missing pixels the GUI has drawn. */
bool aa_gui_dirty(void);

#else /* CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN */

static inline int aa_gui_init(void)
{
	return 0;
}

static inline void aa_gui_apply_yuyv(uint8_t *dst, uint16_t pitch, unsigned int idx)
{
	ARG_UNUSED(dst);
	ARG_UNUSED(pitch);
	ARG_UNUSED(idx);
}

static inline void aa_gui_apply_rgb565(uint16_t *dst, uint16_t pitch, unsigned int idx)
{
	ARG_UNUSED(dst);
	ARG_UNUSED(pitch);
	ARG_UNUSED(idx);
}

static inline bool aa_gui_dirty(void)
{
	return false;
}

#endif /* CONFIG_SAMPLE_AA_HU_SPLIT_SCREEN */

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_GUI_H_ */
