/*
 * Copyright (c) 2023 Arm Limited (or its affiliates). All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM CPU architecture-specific structures
 *
 * Per-CPU architecture-specific data structures for ARM processors.
 */

#ifndef ZEPHYR_INCLUDE_ARM_STRUCTS_H_
#define ZEPHYR_INCLUDE_ARM_STRUCTS_H_

#include <zephyr/types.h>

#if defined(CONFIG_CPU_AARCH32_CORTEX_A) || defined(CONFIG_CPU_AARCH32_CORTEX_R)
/**
 * @brief ARM Cortex-A/R CPU architecture-specific state
 *
 * Per-CPU architecture-specific data for Cortex-A and Cortex-R processors.
 */
struct _cpu_arch {
	int8_t exc_depth;  /**< Current exception nesting depth */
};

#else

/* Default definitions when no architecture specific definitions exist. */

/**
 * @brief Default ARM CPU architecture-specific state
 *
 * Empty per-CPU architecture-specific structure for ARM processors
 * that don't require additional CPU-specific state.
 */
struct _cpu_arch {
#ifdef __cplusplus
	/* This struct will have a size 0 in C which is not allowed in C++ (it'll have a size 1). To
	 * prevent this, we add a 1 byte dummy variable.
	 */
	uint8_t dummy;  /**< Dummy field for C++ compatibility */
#endif
};

#endif

#endif /* ZEPHYR_INCLUDE_ARM_STRUCTS_H_ */
