/*
 * Copyright (c) 2024, Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX memory management declarations.
 */

#ifndef ZEPHYR_INCLUDE_POSIX_SYS_MMAN_H_
#define ZEPHYR_INCLUDE_POSIX_SYS_MMAN_H_

#include <stddef.h>
#include <sys/types.h>

/** Page cannot be accessed */
#define PROT_NONE  0x0
/** Page can be read */
#define PROT_READ  0x1
/** Page can be written */
#define PROT_WRITE 0x2
/** Page can be executed */
#define PROT_EXEC  0x4

/** Share changes */
#define MAP_SHARED  0x1
/** Changes are private */
#define MAP_PRIVATE 0x2
/** Interpret the address argument exactly */
#define MAP_FIXED   0x4

/** Map anonymous memory (for Linux compatibility) */
#define MAP_ANONYMOUS 0x20

/** Perform synchronous writes */
#define MS_SYNC       0x0
/** Perform asynchronous writes */
#define MS_ASYNC      0x1
/** Invalidate mappings */
#define MS_INVALIDATE 0x2

/** Value returned by mmap() in case of error */
#define MAP_FAILED ((void *)-1)

/** Lock currently mapped pages */
#define MCL_CURRENT 0
/** Lock pages that become mapped */
#define MCL_FUTURE  1

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Lock a range of process address space
 *
 * See IEEE 1003.1
 */
int mlock(const void *addr, size_t len);

/**
 * @brief Lock the address space of a process
 *
 * See IEEE 1003.1
 */
int mlockall(int flags);

/**
 * @brief Map pages of memory
 *
 * See IEEE 1003.1
 */
void *mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off);

/**
 * @brief Synchronize memory with physical storage
 *
 * See IEEE 1003.1
 */
int msync(void *addr, size_t length, int flags);

/**
 * @brief Unlock a range of process address space
 *
 * See IEEE 1003.1
 */
int munlock(const void *addr, size_t len);

/**
 * @brief Unlock the address space of a process
 *
 * See IEEE 1003.1
 */
int munlockall(void);

/**
 * @brief Unmap pages of memory
 *
 * See IEEE 1003.1
 */
int munmap(void *addr, size_t len);

/**
 * @brief Open a shared memory object
 *
 * See IEEE 1003.1
 */
int shm_open(const char *name, int oflag, mode_t mode);

/**
 * @brief Remove a shared memory object
 *
 * See IEEE 1003.1
 */
int shm_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_POSIX_SYS_MMAN_H_ */
