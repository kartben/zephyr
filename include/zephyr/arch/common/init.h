/**
 * SPDX-License-Identifier: Apache-2.0
 * Copyright The Zephyr Project Contributors
 */

/**
 * @file
 * @brief Header file for the early boot initialization routines implemented by architectures.
 * @ingroup arch-interface
 */

#ifndef ZEPHYR_INCLUDE_ARCH_COMMON_INIT_H_
#define ZEPHYR_INCLUDE_ARCH_COMMON_INIT_H_

#include <zephyr/toolchain.h>
#include <stddef.h>

FUNC_NORETURN void z_cstart(void);

/**
 * @brief Set memory contents very early during boot
 *
 * Equivalent of memset() usable very early during boot, before the
 * regular (optimized) memset() can be relied on because e.g. hardware
 * is not yet sufficiently initialized.
 *
 * @param dst Destination address
 * @param c Value written to each byte
 * @param n Number of bytes to write
 */
void arch_early_memset(void *dst, int c, size_t n);

/**
 * @brief Copy memory contents very early during boot
 *
 * Equivalent of memcpy() usable very early during boot, before the
 * regular (optimized) memcpy() can be relied on because e.g. hardware
 * is not yet sufficiently initialized.
 *
 * @param dst Destination address
 * @param src Source address
 * @param n Number of bytes to copy
 */
void arch_early_memcpy(void *dst, const void *src, size_t n);

/**
 * @brief Clear the BSS region
 *
 * Zero every byte of the BSS region early during boot, so that all
 * zero-initialized variables start out cleared.
 */
void arch_bss_zero(void);

/**
 * @brief Clear the BSS within the boot region
 *
 * Zero the BSS portion of the boot region, which may contain symbols
 * required by the boot process before paging is initialized. Does
 * nothing when the boot section is not in use.
 */
#ifdef CONFIG_LINKER_USE_BOOT_SECTION
void arch_bss_zero_boot(void);
#else
static inline void arch_bss_zero_boot(void)
{
	/* Do nothing */
}
#endif /* CONFIG_LINKER_USE_BOOT_SECTION */

#endif /* ZEPHYR_INCLUDE_ARCH_COMMON_INIT_H_ */
