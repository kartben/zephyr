/*
 * Copyright (c) 2019 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX poll function and definitions.
 * @ingroup bsd_sockets
 */

#ifndef ZEPHYR_INCLUDE_POSIX_POLL_H_
#define ZEPHYR_INCLUDE_POSIX_POLL_H_

#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of file descriptors passed to poll() */
typedef	unsigned int nfds_t;

/** Poll descriptor structure */
#define pollfd zsock_pollfd

/** Poll for readability */
#define POLLIN ZSOCK_POLLIN
/** Poll for exceptional condition */
#define POLLPRI ZSOCK_POLLPRI
/** Poll for writability */
#define POLLOUT ZSOCK_POLLOUT
/** Error condition occurred (output value only) */
#define POLLERR ZSOCK_POLLERR
/** Connection closed (output value only) */
#define POLLHUP ZSOCK_POLLHUP
/** Invalid file descriptor (output value only) */
#define POLLNVAL ZSOCK_POLLNVAL

/**
 * @brief Wait for events on a set of file descriptors.
 *
 * See IEEE 1003.1
 */
int poll(struct pollfd *fds, int nfds, int timeout);

#ifdef __cplusplus
}
#endif

#endif	/* ZEPHYR_INCLUDE_POSIX_POLL_H_ */
