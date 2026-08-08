/*
 * Copyright (c) 2020, Synopsys, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Metware toolchain linker defs
 * @ingroup internal_api
 *
 * This header file defines the necessary macros used by the linker script for
 * use with the metware linker.
 */

#ifndef ZEPHYR_INCLUDE_LINKER_LINKER_TOOL_MWDT_H_
#define ZEPHYR_INCLUDE_LINKER_LINKER_TOOL_MWDT_H_

/**
 * @def ASSERT
 *
 * Assert on a link-time condition.
 *
 * Expands to nothing, as the mwdt linker does not have this directive.
 *
 * @param x Condition to check
 * @param y Message to report when the condition does not hold
 */
#define ASSERT(x, y)

/**
 * @def SUBALIGN
 *
 * Set the alignment of the input sections of an output section.
 *
 * Mapped to ALIGN(), as the mwdt linker does not have this directive.
 *
 * @param x Alignment in bytes
 */
#define SUBALIGN(x) ALIGN(x)

/**
 * @def GROUP_START
 *
 * Open a group of sections located in one memory area, such as RAM, ROM, etc.
 *
 * Must be paired with GROUP_END() using the same memory area.
 *
 * @param where Name of the memory area
 */
#define GROUP_START(where)

/**
 * @def GROUP_END
 *
 * Close a group of sections opened with GROUP_START().
 *
 * @param where Name of the memory area
 */
#define GROUP_END(where)

/**
 * @def GROUP_LINK_IN
 *
 * Route memory to a specified memory area
 *
 * The GROUP_LINK_IN() macro is located at the end of the section
 * description and tells the linker that this section is located in
 * the memory area specified by the 'where' argument.
 *
 * @param where Destination memory area
 */
#define GROUP_LINK_IN(where) > where

/**
 * The GROUP_ROM_LINK_IN() macro is located at the end of the section
 * description and tells the linker that this a read-only section
 * that is physically placed at the `region` argument.
 *
 */
#define GROUP_ROM_LINK_IN(vregion, lregion) > lregion

/**
 * @def GROUP_DATA_LINK_IN
 *
 * Route memory for read-write sections that are loaded.
 *
 * As GROUP_LINK_IN(), but takes a second argument indicating the
 * memory region (e.g. "ROM") for the load address.  Used for
 * initialized data sections that on XIP platforms must be copied at
 * startup.
 *
 * And, because output directives in GNU ld are "sticky", this must
 * also be used on the first section *after* such an initialized data
 * section, specifying the same memory region (e.g. "RAM") for both
 * vregion and lregion.
 *
 * @param vregion Output VMA
 * @param lregion Output LMA (only used if CONFIG_XIP)
 */
#ifdef CONFIG_XIP
#define GROUP_DATA_LINK_IN(vregion, lregion) > vregion AT > lregion
#else
#define GROUP_DATA_LINK_IN(vregion, lregion) > vregion
#endif

/**
 * Route memory for read-write sections that are NOT loaded; typically this
 * is only used for 'BSS' and 'noinit'.
 */
#ifdef CONFIG_XIP
#define GROUP_NOLOAD_LINK_IN(vregion, lregion) > vregion AT > vregion
#else
#define GROUP_NOLOAD_LINK_IN(vregion, lregion) > vregion
#endif

/**
 * @def SECTION_PROLOGUE
 *
 * The SECTION_PROLOGUE() macro is used to define the beginning of a section.
 *
 * Page alignment has its own parameter since it needs abstraction across the
 * different toolchains. If not required, the 'options' and 'align' parameters
 * should be left blank.
 *
 * @param name Name of the output section
 * @param options Section options, such as (NOLOAD), or left blank
 * @param align Alignment directives, such as SUBALIGN(). May be blank.
 */
#define SECTION_PROLOGUE(name, options, align) name options : align

/**
 * @def SECTION_DATA_PROLOGUE
 *
 * As for SECTION_PROLOGUE(), except that this one must (!) be used
 * for data sections which on XIP platforms will have differing
 * virtual and load addresses (i.e. they'll be copied into RAM at
 * program startup).  Such a section must (!) also use
 * GROUP_DATA_LINK_IN to specify the correct output load address.
 *
 * @param name Name of the output section
 * @param options Section options, or left blank
 * @param align Alignment directives, such as SUBALIGN(). May be blank.
 */
#ifdef CONFIG_XIP
#define SECTION_DATA_PROLOGUE(name, options, align) \
	name options ALIGN(8) : align
#else
#define SECTION_DATA_PROLOGUE(name, options, align) name options : align

#endif

/**
 * @def SORT_BY_NAME
 *
 * Sort the matching input sections by name.
 *
 * Mapped to the SORT() directive of the mwdt linker.
 *
 * @param x Input section wildcard pattern
 */
#define SORT_BY_NAME(x) SORT(x)

#endif /* ZEPHYR_INCLUDE_LINKER_LINKER_TOOL_MWDT_H_ */
