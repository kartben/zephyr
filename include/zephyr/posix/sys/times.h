/*
 * Copyright (c) 2025 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX process times interface.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_TIMES_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_TIMES_H_

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_POSIX_MULTI_PROCESS) || defined(__DOXYGEN__)

#if !defined(_TMS_DECLARED) && !defined(__tms_defined)
/** Process times structure, returned by times() */
struct tms {
	clock_t tms_utime;  /**< User CPU time */
	clock_t tms_stime;  /**< System CPU time */
	clock_t tms_cutime; /**< User CPU time of terminated child processes */
	clock_t tms_cstime; /**< System CPU time of terminated child processes */
};
#define _TMS_DECLARED
#define __tms_defined
#endif

/**
 * @brief Get process and waited-for child process times
 *
 * See IEEE 1003.1
 */
clock_t times(struct tms *buf);

#endif /* _POSIX_MULTI_PROCESS */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS_TIMES_H_ */
