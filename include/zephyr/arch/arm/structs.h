/*
 * Copyright (c) 2023 Arm Limited (or its affiliates). All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 architecture-specific CPU structures
 *
 * This header defines architecture-specific per-CPU structures for ARM
 * AArch32 processors. These structures are used internally by the kernel
 * to maintain CPU-specific state.
 */

#ifndef ZEPHYR_INCLUDE_ARM_STRUCTS_H_
#define ZEPHYR_INCLUDE_ARM_STRUCTS_H_

#include <zephyr/types.h>

#if defined(CONFIG_CPU_AARCH32_CORTEX_A) || defined(CONFIG_CPU_AARCH32_CORTEX_R)
/**
 * @brief Per-CPU architecture-specific data for Cortex-A/R
 *
 * This structure contains architecture-specific data that is maintained
 * on a per-CPU basis for ARM Cortex-A and Cortex-R processors.
 */
struct _cpu_arch {
	/** Current exception nesting depth */
	int8_t exc_depth;
};

#else

/* Default definitions when no architecture specific definitions exist. */

/**
 * @brief Per-CPU architecture-specific data (default/empty)
 *
 * Default empty structure for architectures that don't require
 * per-CPU architecture-specific data. For Cortex-M processors,
 * no additional per-CPU data is needed.
 */
struct _cpu_arch {
#ifdef __cplusplus
	/* This struct will have a size 0 in C which is not allowed in C++ (it'll have a size 1). To
	 * prevent this, we add a 1 byte dummy variable.
	 */
	uint8_t dummy;
#endif
};

#endif

#endif /* ZEPHYR_INCLUDE_ARM_STRUCTS_H_ */
