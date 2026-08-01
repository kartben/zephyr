/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX ioctl function and request definitions.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_IOCTL_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_IOCTL_H_

#include <zephyr/sys/fdtable.h>

/** Set non-blocking mode */
#define FIONBIO ZFD_IOCTL_FIONBIO
/** Get the number of bytes available to read */
#define FIONREAD ZFD_IOCTL_FIONREAD
/** Get the number of bytes queued for TCP TX which have not yet been acknowledged */
#define FIONWRITE ZFD_IOCTL_FIONWRITE

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Perform control operations on an open file descriptor
 *
 * See IEEE 1003.1
 */
int ioctl(int fd, unsigned long request, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS_IOCTL_H_ */
