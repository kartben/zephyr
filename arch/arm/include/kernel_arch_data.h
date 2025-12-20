/*
 * Copyright (c) 2013-2016 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Private kernel definitions (ARM)
 *
 * This file contains private kernel structures definitions and various
 * other definitions for the ARM Cortex-A/R/M processor architecture family.
 *
 * This file is also included by assembly language files which must #define
 * _ASMLANGUAGE before including this header file.  Note that kernel
 * assembly source files obtains structure offset values via "absolute symbols"
 * in the offsets.o module.
 */

#ifndef ZEPHYR_ARCH_ARM_INCLUDE_KERNEL_ARCH_DATA_H_
#define ZEPHYR_ARCH_ARM_INCLUDE_KERNEL_ARCH_DATA_H_

#include <zephyr/toolchain.h>
#include <zephyr/linker/sections.h>
#include <zephyr/arch/cpu.h>

#if defined(CONFIG_CPU_CORTEX_M)
#include <cortex_m/stack.h>
#include <cortex_m/exception.h>
#elif defined(CONFIG_CPU_AARCH32_CORTEX_R) || defined(CONFIG_CPU_AARCH32_CORTEX_A)
#include <cortex_a_r/stack.h>
#include <cortex_a_r/exception.h>
#endif

#ifndef _ASMLANGUAGE
#include <zephyr/kernel.h>
#include <zephyr/types.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/sys/atomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @typedef _esf_t
 * @brief Exception stack frame type
 *
 * Architecture-specific exception stack frame. Contains the register
 * state saved when an exception occurs.
 */
typedef struct arch_esf _esf_t;

/**
 * @typedef _basic_sf_t
 * @brief Basic stack frame type
 *
 * Basic stack frame for storing minimal context during context switches.
 */
typedef struct __basic_sf _basic_sf_t;

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
/**
 * @typedef _fpu_sf_t
 * @brief FPU stack frame type
 *
 * FPU register context saved during context switches when FPU sharing
 * is enabled.
 */
typedef struct __fpu_sf _fpu_sf_t;
#endif

#ifdef CONFIG_ARM_MPU
/**
 * @brief ARM MPU memory partition descriptor
 *
 * Describes a memory partition for ARM MPU configuration, including
 * the start address, size, and memory attributes.
 */
struct z_arm_mpu_partition {
	uintptr_t start;  /**< Start address of the memory partition */
	size_t size;      /**< Size of the memory partition in bytes */
	k_mem_partition_attr_t attr;  /**< Memory partition attributes */
};
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_ARCH_ARM_INCLUDE_KERNEL_ARCH_DATA_H_ */
