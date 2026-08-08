/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the execute in place (XIP) data copy routine implemented by architectures.
 * @ingroup arch-interface
 */

#ifndef ZEPHYR_INCLUDE_ARCH_COMMON_XIP_H_
#define ZEPHYR_INCLUDE_ARCH_COMMON_XIP_H_

#ifndef _ASMLANGUAGE
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy the data sections from ROM to RAM
 *
 * Copy the writable data sections of the image from their load address
 * in ROM to their runtime location in RAM early during boot. Does
 * nothing when the image does not execute in place.
 */
#ifdef CONFIG_XIP
void arch_data_copy(void);
#else
static inline void arch_data_copy(void)
{
	/* Do nothing */
}
#endif /* CONFIG_XIP */
#ifdef __cplusplus
}
#endif

#endif	/* _ASMLANGUAGE */
#endif /* ZEPHYR_INCLUDE_ARCH_COMMON_XIP_H_ */
