/*
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX semaphore support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SEMAPHORE_H_
#define ZEPHYR_INCLUDE_POSIX_SEMAPHORE_H_

#include <time.h>

#include <zephyr/posix/posix_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Value returned by sem_open() in case of error */
#define SEM_FAILED ((sem_t *) 0)

/**
 * @brief Destroy an unnamed semaphore
 *
 * See IEEE 1003.1
 */
int sem_destroy(sem_t *semaphore);

/**
 * @brief Get the value of a semaphore
 *
 * See IEEE 1003.1
 */
int sem_getvalue(sem_t *ZRESTRICT semaphore, int *ZRESTRICT value);

/**
 * @brief Initialize an unnamed semaphore
 *
 * See IEEE 1003.1
 */
int sem_init(sem_t *semaphore, int pshared, unsigned int value);

/**
 * @brief Unlock a semaphore
 *
 * See IEEE 1003.1
 */
int sem_post(sem_t *semaphore);

/**
 * @brief Lock a semaphore, with a time limit
 *
 * See IEEE 1003.1
 */
int sem_timedwait(sem_t *ZRESTRICT semaphore, struct timespec *ZRESTRICT abstime);

/**
 * @brief Lock a semaphore only if it is not currently locked
 *
 * See IEEE 1003.1
 */
int sem_trywait(sem_t *semaphore);

/**
 * @brief Lock a semaphore
 *
 * See IEEE 1003.1
 */
int sem_wait(sem_t *semaphore);

/**
 * @brief Initialize and open a named semaphore
 *
 * See IEEE 1003.1
 */
sem_t *sem_open(const char *name, int oflags, ...);

/**
 * @brief Remove a named semaphore
 *
 * See IEEE 1003.1
 */
int sem_unlink(const char *name);

/**
 * @brief Close a named semaphore
 *
 * See IEEE 1003.1
 */
int sem_close(sem_t *sem);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SEMAPHORE_H_ */
