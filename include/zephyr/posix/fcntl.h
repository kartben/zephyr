/*
 * Copyright (c) 2018 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX file control definitions.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_FCNTL_H_
#define ZEPHYR_INCLUDE_POSIX_FCNTL_H_

#include <zephyr/sys/fdtable.h>

/** Set the file offset to the end of the file prior to each write */
#define O_APPEND   ZVFS_O_APPEND
/** Create the file if it does not exist */
#define O_CREAT    ZVFS_O_CREAT
/** Fail if the file exists and O_CREAT is also set */
#define O_EXCL     ZVFS_O_EXCL
/** Use non-blocking I/O */
#define O_NONBLOCK ZVFS_O_NONBLOCK
/** Truncate the file to zero length on open */
#define O_TRUNC    ZVFS_O_TRUNC

/** Mask for the file access modes */
#define O_ACCMODE (ZVFS_O_RDONLY | ZVFS_O_RDWR | ZVFS_O_WRONLY)

/** Open for reading only */
#define O_RDONLY ZVFS_O_RDONLY
/** Open for reading and writing */
#define O_RDWR   ZVFS_O_RDWR
/** Open for writing only */
#define O_WRONLY ZVFS_O_WRONLY

/** fcntl() command to duplicate a file descriptor */
#define F_DUPFD ZVFS_F_DUPFD
/** fcntl() command to get the file status flags */
#define F_GETFL ZVFS_F_GETFL
/** fcntl() command to set the file status flags */
#define F_SETFL ZVFS_F_SETFL

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open a file.
 *
 * See IEEE 1003.1
 */
int open(const char *name, int flags, ...);

/**
 * @brief Perform a control operation on an open file descriptor.
 *
 * See IEEE 1003.1
 */
int fcntl(int fildes, int cmd, ...);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_FCNTL_H_ */
