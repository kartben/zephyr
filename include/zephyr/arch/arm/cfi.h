/*
 * Copyright (c) 2025 NXP Semicondutors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 Call Frame Information (CFI) definitions
 *
 * This header provides ARM-specific CFI (Call Frame Information) macros
 * for debugging and stack unwinding purposes. CFI directives are used
 * by debuggers to properly unwind the call stack.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_CFI_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_CFI_H_

/**
 * @brief Mark the return address as undefined for CFI
 *
 * This macro emits a CFI directive that marks the link register (LR)
 * as undefined. This is used to indicate that the return address
 * cannot be recovered from the current stack frame, typically used
 * at the bottom of the call stack or in special contexts.
 */
#define ARCH_CFI_UNDEFINED_RETURN_ADDRESS() __asm__ volatile(".cfi_undefined lr")

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_CFI_H_ */
