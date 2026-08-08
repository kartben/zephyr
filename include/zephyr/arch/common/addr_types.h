/* Address types (virtual, physical, etc) definitions */

/*
 * Copyright (c) 2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the virtual and physical address type definitions.
 * @ingroup arch-interface
 */

#ifndef ZEPHYR_INCLUDE_ARCH_COMMON_ADDR_TYPES_H_
#define ZEPHYR_INCLUDE_ARCH_COMMON_ADDR_TYPES_H_

#ifndef _ASMLANGUAGE
/** Physical address type */
typedef uintptr_t paddr_t;
/** Virtual address type */
typedef void *vaddr_t;
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_COMMON_ADDR_TYPES_H_ */
