/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 *
 * Placeholder so edge264_internal.h can include <unistd.h> under the
 * minimal C library (no POSIX unistd). ssize_t is provided by types.h.
 */

#ifndef EDGE264_POSIX_STUBS_UNISTD_H
#define EDGE264_POSIX_STUBS_UNISTD_H

#include <sys/types.h>

#ifndef _SC_NPROCESSORS_ONLN
#define _SC_NPROCESSORS_ONLN 1
#endif

#ifndef _SC_PAGESIZE
#define _SC_PAGESIZE 2
#endif

static inline long sysconf(int name)
{
	(void)name;
	return 1;
}

#endif /* EDGE264_POSIX_STUBS_UNISTD_H */
