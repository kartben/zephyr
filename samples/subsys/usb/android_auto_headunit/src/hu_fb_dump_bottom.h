/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_BOTTOM_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_BOTTOM_H_

int hu_fb_dump_bottom_open(const char *path);
long hu_fb_dump_bottom_write(int fd, const void *buf, unsigned long len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_HU_FB_DUMP_BOTTOM_H_ */
