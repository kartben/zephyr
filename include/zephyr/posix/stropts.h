/*
 * Copyright (c) 2024 Abhinav Srivastava
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX STREAMS interface support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_STROPTS_H_
#define ZEPHYR_INCLUDE_POSIX_STROPTS_H_

/** Send or receive a high priority STREAMS message */
#define RS_HIPRI BIT(0)

#ifdef __cplusplus
extern "C" {
#endif

/** Buffer descriptor used to send and receive STREAMS messages */
struct strbuf {
	int maxlen; /**< Maximum buffer length */
	int len;    /**< Length of data */
	char *buf;  /**< Pointer to buffer */
};

/**
 * @brief Send a message on a STREAM
 *
 * See IEEE 1003.1
 */
int putmsg(int fildes, const struct strbuf *ctlptr, const struct strbuf *dataptr, int flags);

/**
 * @brief Detach a name from a STREAMS-based file descriptor
 *
 * See IEEE 1003.1
 */
int fdetach(const char *path);

/**
 * @brief Attach a STREAMS-based file descriptor to a file in the file system name space
 *
 * See IEEE 1003.1
 */
int fattach(int fildes, const char *path);

/**
 * @brief Receive the next message from a STREAM
 *
 * See IEEE 1003.1
 */
int getmsg(int fildes, struct strbuf *ctlptr, struct strbuf *dataptr, int *flagsp);

/**
 * @brief Receive the next message from a STREAM, with finer priority control
 *
 * See IEEE 1003.1
 */
int getpmsg(int fildes, struct strbuf *ctlptr, struct strbuf *dataptr, int *bandp, int *flagsp);

/**
 * @brief Test whether a file descriptor is associated with a STREAM
 *
 * See IEEE 1003.1
 */
int isastream(int fildes);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_STROPTS_H_ */
