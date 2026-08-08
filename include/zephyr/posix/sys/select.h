/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX select function and file descriptor set definitions.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_SELECT_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_SELECT_H_

#include <zephyr/posix/posix_types.h>
#include <zephyr/sys/fdtable.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of file descriptors in an fd_set structure */
#define FD_SETSIZE ZVFS_FD_SETSIZE

/** Set of file descriptors for use with select() and pselect() */
typedef struct zvfs_fd_set fd_set;

struct timeval;

/**
 * @brief Synchronous I/O multiplexing, with a signal mask
 *
 * See IEEE 1003.1
 */
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
	    const struct timespec *timeout, const void *sigmask);

/**
 * @brief Synchronous I/O multiplexing
 *
 * See IEEE 1003.1
 */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *errorfds, struct timeval *timeout);

/**
 * @brief Remove a file descriptor from a file descriptor set
 *
 * See IEEE 1003.1
 */
void FD_CLR(int fd, fd_set *fdset);

/**
 * @brief Check whether a file descriptor is a member of a file descriptor set
 *
 * See IEEE 1003.1
 */
int FD_ISSET(int fd, fd_set *fdset);

/**
 * @brief Add a file descriptor to a file descriptor set
 *
 * See IEEE 1003.1
 */
void FD_SET(int fd, fd_set *fdset);

/**
 * @brief Initialize a file descriptor set to be empty
 *
 * See IEEE 1003.1
 */
void FD_ZERO(fd_set *fdset);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS_SELECT_H_ */
