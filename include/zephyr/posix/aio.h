/*
 * Copyright 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX asynchronous I/O support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_AIO_H_
#define ZEPHYR_INCLUDE_POSIX_AIO_H_

#include <signal.h>
#include <sys/types.h>
#include <time.h>

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Asynchronous I/O control block
 */
struct aiocb {
	int aio_fildes;               /**< File descriptor */
	off_t aio_offset;             /**< File offset */
	volatile void *aio_buf;       /**< Location of buffer */
	size_t aio_nbytes;            /**< Length of transfer */
	int aio_reqprio;              /**< Request priority offset */
	struct sigevent aio_sigevent; /**< Signal number and value */
	int aio_lio_opcode;           /**< Operation to be performed */
};

#if _POSIX_C_SOURCE >= 200112L

int aio_cancel(int fildes, struct aiocb *aiocbp);
int aio_error(const struct aiocb *aiocbp);
int aio_fsync(int filedes, struct aiocb *aiocbp);
int aio_read(struct aiocb *aiocbp);
ssize_t aio_return(struct aiocb *aiocbp);
int aio_suspend(const struct aiocb *const list[], int nent, const struct timespec *timeout);
int aio_write(struct aiocb *aiocbp);
int lio_listio(int mode, struct aiocb *const ZRESTRICT list[], int nent,
	       struct sigevent *ZRESTRICT sig);

#endif /* _POSIX_C_SOURCE >= 200112L */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_AIO_H_ */
