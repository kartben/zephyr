/*
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX directory stream operations.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_DIRENT_H_
#define ZEPHYR_INCLUDE_POSIX_DIRENT_H_

#include <zephyr/posix/sys/dirent.h>
#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

#if (_POSIX_C_SOURCE >= 200809L) || (_XOPEN_SOURCE >= 700)
int alphasort(const struct dirent **d1, const struct dirent **d2);
#endif
/**
 * @brief Close a directory stream.
 *
 * See IEEE 1003.1
 */
int closedir(DIR *dirp);
#if (_POSIX_C_SOURCE >= 200809L) || (_XOPEN_SOURCE >= 700)
int dirfd(DIR *dirp);
#endif
/**
 * @brief Open a directory stream for the directory associated with a file descriptor.
 *
 * See IEEE 1003.1
 */
DIR *fdopendir(int fd);
/**
 * @brief Open a directory stream.
 *
 * See IEEE 1003.1
 */
DIR *opendir(const char *dirname);
/**
 * @brief Read the next entry from a directory stream.
 *
 * See IEEE 1003.1
 */
struct dirent *readdir(DIR *dirp);
#if (_POSIX_C_SOURCE >= 199506L) || (_XOPEN_SOURCE >= 500)
int readdir_r(DIR *ZRESTRICT dirp, struct dirent *ZRESTRICT entry,
	      struct dirent **ZRESTRICT result);
#endif
/**
 * @brief Reset a directory stream to the beginning of the directory.
 *
 * See IEEE 1003.1
 */
void rewinddir(DIR *dirp);
#if (_POSIX_C_SOURCE >= 200809L) || (_XOPEN_SOURCE >= 700)
int scandir(const char *dir, struct dirent ***namelist, int (*sel)(const struct dirent *),
	    int (*compar)(const struct dirent **, const struct dirent **));
#endif
#if defined(_XOPEN_SOURCE)
void seekdir(DIR *dirp, long loc);
long telldir(DIR *dirp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_DIRENT_H_ */
