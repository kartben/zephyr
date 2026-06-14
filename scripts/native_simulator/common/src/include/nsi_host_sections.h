/*
 * Copyright (c) 2024 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Host-OS abstraction for collecting linker "iterable" sections.
 *
 * The native simulator registers tables of callbacks (tasks, HW events, ...) by
 * placing entries in dedicated linker sections and iterating over them at
 * runtime.
 *
 * On GNU/Linux (ELF + GNU ld) the section is named with the entry priority
 * encoded in the section name, and the linker script sorts and delimits the
 * entries with __start/__stop style symbols.
 *
 * macOS (Mach-O + ld64) supports neither GNU linker scripts nor the priority
 * encoded section-name sorting: ld64 only coalesces same-named input sections,
 * in link order, with no sorting. So on macOS every family of entries is placed
 * in a single short, fixed-name section (Mach-O section names are limited to 16
 * characters), the section bounds are obtained at runtime with getsectiondata(),
 * and the priority ordering is reconstructed at runtime from a priority field
 * carried by each entry.
 *
 * The `regular,no_dead_strip` section attribute is the Mach-O equivalent of the
 * GNU ld KEEP(): it prevents `-dead_strip` from discarding entries that are only
 * reachable through getsectiondata() (i.e. never referenced by a symbol).
 */

#ifndef NSI_COMMON_SRC_INCL_NSI_HOST_SECTIONS_H
#define NSI_COMMON_SRC_INCL_NSI_HOST_SECTIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__APPLE__)

#include <stddef.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h> /* Declares _mh_execute_header */

/*
 * Place an entry into the macOS Mach-O section `__DATA,<sect>`. `sect` must be a
 * string literal of at most 16 characters. The section is marked no_dead_strip
 * (the Mach-O analog of GNU ld's KEEP()) and the symbol is marked used so the
 * compiler keeps it even though nothing references it directly.
 */
#define NSI_HOST_SECTION(sect) \
	__attribute__((__used__, __section__("__DATA," sect ",regular,no_dead_strip")))

/*
 * Retrieve the [start, start + count) bounds of the entries collected in the
 * macOS section `__DATA,<sect>`. `type` is the element type, `start` an lvalue of
 * `type *` and `count` an lvalue of an integer type.
 */
#define NSI_HOST_GET_SECTION(type, sect, start, count) \
	do { \
		unsigned long _nsi_sz = 0; \
		(start) = (type *)getsectiondata(&_mh_execute_header, "__DATA", sect, &_nsi_sz); \
		(count) = (start) ? (_nsi_sz / sizeof(type)) : 0; \
	} while (0)

#endif /* defined(__APPLE__) */

#ifdef __cplusplus
}
#endif

#endif /* NSI_COMMON_SRC_INCL_NSI_HOST_SECTIONS_H */
