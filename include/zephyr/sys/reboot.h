/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Common target reboot functionality
 * @ingroup os_services
 *
 * @details See subsys/os/Kconfig and the reboot help for details.
 */

#ifndef ZEPHYR_INCLUDE_SYS_REBOOT_H_
#define ZEPHYR_INCLUDE_SYS_REBOOT_H_

#include <zephyr/toolchain.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Warm reboot: restart the system without a full hardware reset.
 *
 * The exact behavior is architecture and platform specific; not all
 * platforms distinguish reboot types.
 */
#define SYS_REBOOT_WARM 0

/**
 * @brief Cold reboot: restart the system from a state equivalent to power-on.
 *
 * The exact behavior is architecture and platform specific.
 */
#define SYS_REBOOT_COLD 1

/**
 * @brief Reboot the system
 *
 * Reboot the system in the manner specified by @a type.  Not all architectures
 * or platforms support the various reboot types (SYS_REBOOT_COLD,
 * SYS_REBOOT_WARM).
 *
 * When successful, this routine does not return.
 */
FUNC_NORETURN void sys_reboot(int type);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SYS_REBOOT_H_ */
