/**
 * @file
 * @brief ARM AArch32 NMI handling routines
 *
 * This header provides the interface for handling Non-Maskable Interrupts (NMI)
 * on ARM AArch32 processors. It allows for runtime installation of a custom
 * NMI handler when CONFIG_RUNTIME_NMI is enabled.
 */

/*
 * Copyright (c) 2015 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_NMI_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_NMI_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(_ASMLANGUAGE) && defined(CONFIG_RUNTIME_NMI)
/**
 * @brief Install a custom runtime NMI handler
 *
 * This function allows platform code to install a custom NMI handler
 * at runtime. It should be called after the console is initialized
 * if the handler is meant to output to the console.
 *
 * @param pHandler Pointer to the custom NMI handler function.
 */
extern void z_arm_nmi_set_handler(void (*pHandler)(void));
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_NMI_H_ */
