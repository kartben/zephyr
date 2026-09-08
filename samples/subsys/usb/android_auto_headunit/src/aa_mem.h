/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MEM_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MEM_H_

/*
 * The framebuffer and the reassembly buffer are hundreds of kilobytes and do
 * not fit the internal RAM of a typical board, so they are placed in external
 * RAM where one is available.
 */
#ifdef CONFIG_SAMPLE_AA_HU_BUFFERS_IN_EXT_RAM
#define AA_HU_BIG_BUF __attribute__((section(CONFIG_SAMPLE_AA_HU_BUFFERS_SECTION)))
#else
#define AA_HU_BIG_BUF
#endif

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_MEM_H_ */
