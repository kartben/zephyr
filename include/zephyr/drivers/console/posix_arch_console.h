/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the POSIX architecture console driver helpers.
 * @ingroup console_api
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_CONSOLE_POSIX_ARCH_CONSOLE_H_
#define ZEPHYR_INCLUDE_DRIVERS_CONSOLE_POSIX_ARCH_CONSOLE_H_

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Display any console output that is still buffered.
 *
 * Ensure that everything sent to the console so far is displayed, even
 * if the last line was not yet terminated with a newline.
 */
void posix_flush_stdout(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_CONSOLE_POSIX_ARCH_CONSOLE_H_ */
