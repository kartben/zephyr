/*
 * Copyright (c) 2023 Meta
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX system identification support.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_UTSNAME_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_UTSNAME_H_

#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

/* These are for compatibility / practicality */
#define _UTSNAME_NODENAME_LENGTH                                                                   \
	COND_CODE_1(CONFIG_POSIX_SINGLE_PROCESS, (CONFIG_POSIX_UNAME_VERSION_LEN), (0))
#define _UTSNAME_VERSION_LENGTH                                                                    \
	COND_CODE_1(CONFIG_POSIX_SINGLE_PROCESS, (CONFIG_POSIX_UNAME_VERSION_LEN), (0))

/** Structure describing the current system, returned by uname() */
struct utsname {
	/** Name of this implementation of the operating system */
	char sysname[sizeof("Zephyr")];
	/** Name of this node within the communications network */
	char nodename[_UTSNAME_NODENAME_LENGTH + 1];
	/** Current release level of this implementation */
	char release[sizeof("99.99.99-rc1")];
	/** Current version level of this release */
	char version[_UTSNAME_VERSION_LENGTH + 1];
	/** Name of the hardware type on which the system is running */
	char machine[sizeof(CONFIG_ARCH)];
};

/**
 * @brief Get the name of the current system
 *
 * See IEEE 1003.1
 */
int uname(struct utsname *name);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS_UTSNAME_H_ */
