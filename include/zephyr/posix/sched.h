/*
 * Copyright (c) 2018-2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX execution scheduling support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SCHED_H_
#define ZEPHYR_INCLUDE_POSIX_SCHED_H_

#include <zephyr/kernel.h>
#include <zephyr/posix/posix_types.h>

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Other mandatory scheduling policy.
 *
 * Must be numerically distinct. May execute identically to SCHED_RR or
 * SCHED_FIFO. For Zephyr this is a pseudonym for SCHED_RR.
 */
#define SCHED_OTHER 0

/** Cooperative scheduling policy */
#define SCHED_FIFO 1

/** Priority based preemptive scheduling policy */
#define SCHED_RR 2

#if defined(CONFIG_MINIMAL_LIBC) || defined(CONFIG_PICOLIBC) || defined(CONFIG_ARMCLANG_STD_LIBC) \
	|| defined(CONFIG_ARCMWDT_LIBC)
struct sched_param {
	int sched_priority;
};
#endif

/**
 * @brief Yield the processor
 *
 * See IEEE 1003.1
 */
int sched_yield(void);

/**
 * @brief Get minimum priority value for a given policy
 *
 * See IEEE 1003.1
 */
int sched_get_priority_min(int policy);

/**
 * @brief Get maximum priority value for a given policy
 *
 * See IEEE 1003.1
 */
int sched_get_priority_max(int policy);

/**
 * @brief Get scheduling parameters
 *
 * See IEEE 1003.1
 */
int sched_getparam(pid_t pid, struct sched_param *param);

/**
 * @brief Get scheduling policy
 *
 * See IEEE 1003.1
 */
int sched_getscheduler(pid_t pid);

/**
 * @brief Set scheduling parameters
 *
 * See IEEE 1003.1
 */
int sched_setparam(pid_t pid, const struct sched_param *param);

/**
 * @brief Set scheduling policy
 *
 * See IEEE 1003.1
 */
int sched_setscheduler(pid_t pid, int policy, const struct sched_param *param);

/**
 * @brief Get execution time limits
 *
 * See IEEE 1003.1
 */
int sched_rr_get_interval(pid_t pid, struct timespec *interval);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SCHED_H_ */
