/*
 * Copyright (c) 2023 Arm Limited (or its affiliates). All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Definitions for ARM Cortex-A/R boot code
 *
 * Boot-time definitions and offsets for Cortex-A and Cortex-R processor
 * initialization. Includes boot parameter structure offsets used by
 * assembly boot code.
 */

#ifndef _BOOT_H_
#define _BOOT_H_

#ifndef _ASMLANGUAGE

/**
 * @brief Pointer to the vector table
 *
 * Array of function pointers representing the exception vector table.
 */
extern void *_vector_table[];

/**
 * @brief System startup entry point
 *
 * The main entry point for system initialization. This function is called
 * from the reset vector and performs early initialization before jumping
 * to the kernel.
 */
extern void __start(void);

#endif /* _ASMLANGUAGE */

/* Offsets into the boot_params structure */

/** @brief Offset to Multiprocessor ID (MPID) in boot parameters */
#define BOOT_PARAM_MPID_OFFSET		0
/** @brief Offset to IRQ mode stack pointer in boot parameters */
#define BOOT_PARAM_IRQ_SP_OFFSET	4
/** @brief Offset to FIQ mode stack pointer in boot parameters */
#define BOOT_PARAM_FIQ_SP_OFFSET	8
/** @brief Offset to Abort mode stack pointer in boot parameters */
#define BOOT_PARAM_ABT_SP_OFFSET	12
/** @brief Offset to Undefined mode stack pointer in boot parameters */
#define BOOT_PARAM_UDF_SP_OFFSET	16
/** @brief Offset to Supervisor mode stack pointer in boot parameters */
#define BOOT_PARAM_SVC_SP_OFFSET	20
/** @brief Offset to System mode stack pointer in boot parameters */
#define BOOT_PARAM_SYS_SP_OFFSET	24
/** @brief Offset to voting/synchronization field in boot parameters */
#define BOOT_PARAM_VOTING_OFFSET	28

#endif /* _BOOT_H_ */
