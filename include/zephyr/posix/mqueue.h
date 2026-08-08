/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX message queue support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_MQUEUE_H_
#define ZEPHYR_INCLUDE_POSIX_MQUEUE_H_

#include <time.h>
#include <signal.h>

#include <zephyr/kernel.h>
#include <zephyr/posix/fcntl.h>
#include <zephyr/posix/sys/stat.h>
#include <zephyr/posix/posix_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Message queue descriptor */
typedef void *mqd_t;

/**
 * @brief Message queue attributes
 */
struct mq_attr {
	long mq_flags;   /**< Message queue flags */
	long mq_maxmsg;  /**< Maximum number of messages */
	long mq_msgsize; /**< Maximum message size (in bytes) */
	long mq_curmsgs; /**< Number of messages currently queued */
};

/**
 * @brief Open a message queue.
 *
 * See IEEE 1003.1
 */
mqd_t mq_open(const char *name, int oflags, ...);

/**
 * @brief Close a message queue descriptor.
 *
 * See IEEE 1003.1
 */
int mq_close(mqd_t mqdes);

/**
 * @brief Remove a message queue.
 *
 * See IEEE 1003.1
 */
int mq_unlink(const char *name);

/**
 * @brief Get message queue attributes.
 *
 * See IEEE 1003.1
 */
int mq_getattr(mqd_t mqdes, struct mq_attr *mqstat);

/**
 * @brief Receive a message from a message queue.
 *
 * See IEEE 1003.1
 */
int mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
		   unsigned int *msg_prio);

/**
 * @brief Send a message to a message queue.
 *
 * See IEEE 1003.1
 */
int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
	    unsigned int msg_prio);

/**
 * @brief Set message queue attributes.
 *
 * See IEEE 1003.1
 */
int mq_setattr(mqd_t mqdes, const struct mq_attr *mqstat,
	       struct mq_attr *omqstat);

/**
 * @brief Receive a message from a message queue, waiting up to a given time.
 *
 * See IEEE 1003.1
 */
int mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
			unsigned int *msg_prio, const struct timespec *abstime);

/**
 * @brief Send a message to a message queue, waiting up to a given time.
 *
 * See IEEE 1003.1
 */
int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
		 unsigned int msg_prio, const struct timespec *abstime);

/**
 * @brief Notify a thread when a message is available in a message queue.
 *
 * See IEEE 1003.1
 */
int mq_notify(mqd_t mqdes, const struct sigevent *notification);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_MQUEUE_H_ */
