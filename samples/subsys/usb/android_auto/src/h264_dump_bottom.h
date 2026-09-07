/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_BOTTOM_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_BOTTOM_H_

/* Host side of the dump, compiled into the native simulator runner */

int aa_dump_bottom_open(const char *path);
long aa_dump_bottom_write(int fd, const void *buf, unsigned long len);
void aa_dump_bottom_close(int fd);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_H264_DUMP_BOTTOM_H_ */
