/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_SAMPLE_AA_HU_FB_DUMP

/** @brief Open the framebuffer dump file on the host. */
int hu_fb_dump_open(void);

/** @brief Append one RGB565 framebuffer to the dump. */
int hu_fb_dump_write(const uint16_t *fb, size_t pixels);

#else

static inline int hu_fb_dump_open(void)
{
	return 0;
}

static inline int hu_fb_dump_write(const uint16_t *fb, size_t pixels)
{
	ARG_UNUSED(fb);
	ARG_UNUSED(pixels);
	return 0;
}

#endif /* CONFIG_SAMPLE_AA_HU_FB_DUMP */

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_H_ */
