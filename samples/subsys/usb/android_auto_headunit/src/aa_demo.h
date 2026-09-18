/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_H_

#include <errno.h>

#ifdef CONFIG_SAMPLE_AA_HU_DEMO_CLIP

/**
 * @brief Bring up the display and play the clip built into the image.
 *
 * Takes the place of aa_hu_session_start() where no phone can be attached.
 *
 * @retval 0 Success.
 * @retval -ENODEV The display is not ready.
 * @retval -ENOMEM The decoder or the GUI could not be allocated.
 */
int aa_hu_demo_start(void);

#else

static inline int aa_hu_demo_start(void)
{
	return -ENOTSUP;
}

#endif /* CONFIG_SAMPLE_AA_HU_DEMO_CLIP */

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_DEMO_H_ */
