/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 per-thread architecture definitions
 *
 * This file contains ARM AArch32-specific definitions for thread-related
 * structures including struct _thread_arch and struct _callee_saved.
 * These structures are necessary to instantiate instances of struct k_thread.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

/**
 * @brief Callee-saved register structure for ARM AArch32
 *
 * This structure contains the registers that must be preserved across
 * function calls according to the ARM ABI. These registers are saved
 * and restored during context switches.
 */
struct _callee_saved {
	uint32_t v1;  /**< Register r4 */
	uint32_t v2;  /**< Register r5 */
	uint32_t v3;  /**< Register r6 */
	uint32_t v4;  /**< Register r7 */
	uint32_t v5;  /**< Register r8 */
	uint32_t v6;  /**< Register r9 */
	uint32_t v7;  /**< Register r10 */
	uint32_t v8;  /**< Register r11 */
	uint32_t psp; /**< Process stack pointer (r13) */
#ifdef CONFIG_USE_SWITCH
	uint32_t lr;  /**< Link register */
#endif
};

typedef struct _callee_saved _callee_saved_t;

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
/**
 * @brief Preemptible floating-point register structure
 *
 * This structure contains floating-point registers that must be preserved
 * during preemption. Registers s16-s31 (d8-d15, q4-q7) must be preserved
 * across subroutine calls per the ARM ABI.
 *
 * @note Registers s0-s15 (d0-d7, q0-q3) do not have to be preserved and can
 *       be used for passing arguments or returning results.
 * @note Registers d16-d31 (q8-q15) do not have to be preserved.
 */
struct _preempt_float {
	float  s16;  /**< FP register s16 */
	float  s17;  /**< FP register s17 */
	float  s18;  /**< FP register s18 */
	float  s19;  /**< FP register s19 */
	float  s20;  /**< FP register s20 */
	float  s21;  /**< FP register s21 */
	float  s22;  /**< FP register s22 */
	float  s23;  /**< FP register s23 */
	float  s24;  /**< FP register s24 */
	float  s25;  /**< FP register s25 */
	float  s26;  /**< FP register s26 */
	float  s27;  /**< FP register s27 */
	float  s28;  /**< FP register s28 */
	float  s29;  /**< FP register s29 */
	float  s30;  /**< FP register s30 */
	float  s31;  /**< FP register s31 */
};
#endif

#if defined(CONFIG_ARM_PAC_PER_THREAD)
/**
 * @brief Pointer Authentication Code (PAC) keys structure
 *
 * This structure holds the PAC keys for per-thread pointer authentication
 * on processors that support ARM Pointer Authentication.
 */
struct pac_keys {
	uint32_t key_0;  /**< PAC key 0 */
	uint32_t key_1;  /**< PAC key 1 */
	uint32_t key_2;  /**< PAC key 2 */
	uint32_t key_3;  /**< PAC key 3 */
};
#endif

/**
 * @brief ARM AArch32 thread-specific architecture data
 *
 * This structure contains architecture-specific thread data for ARM AArch32.
 * It stores interrupt locking state, context switch return values, and
 * optional FPU and security-related state.
 */
struct _thread_arch {

	/** Interrupt locking key (BASEPRI value) */
	uint32_t basepri;

	/** Return value storage for context switch (r0 cannot be written reliably) */
	uint32_t swap_return_value;

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	/**
	 * Preemptible floating point context.
	 *
	 * @note No cooperative floating point register set structure exists for
	 *       Cortex-M as it automatically saves the necessary registers
	 *       in its exception stack frame.
	 */
	struct _preempt_float  preempt_float;
#endif

#if defined(CONFIG_CPU_AARCH32_CORTEX_A) || defined(CONFIG_CPU_AARCH32_CORTEX_R)
	/** Exception nesting depth for Cortex-A/R processors */
	int8_t exception_depth;
#endif

#if defined(CONFIG_ARM_STORE_EXC_RETURN) || defined(CONFIG_USERSPACE)
	/**
	 * @brief Thread mode status variable
	 *
	 * This variable holds several thread status flags as follows:
	 *
	 * Byte 0:
	 * - Bits 4-7: Reserved
	 * - Bit 3: Guard FLOAT - indicates long MPU stack guard size
	 * - Bit 2: Reserved (deprecated, use FType instead)
	 * - Bit 1: Reserved
	 * - Bit 0: Privilege mode (mirrors CONTROL.nPRIV flag)
	 *
	 * Byte 1 (bits 8-15): Least significant byte of EXC_RETURN:
	 * - Bit 15: Reserved
	 * - Bit 14: S (Stack used)
	 * - Bit 13: DCRS (Default callee register stacking)
	 * - Bit 12: FType (FPU context type)
	 * - Bit 11: Mode (Thread or Handler)
	 * - Bit 10: SPSel (Stack pointer selection)
	 * - Bit 9: Reserved
	 * - Bit 8: ES (Exception Secure)
	 */
	union {
		uint32_t mode;

#if defined(CONFIG_ARM_STORE_EXC_RETURN)
		struct {
			uint8_t mode_bits;       /**< Mode status bits (byte 0) */
			uint8_t mode_exc_return; /**< EXC_RETURN LSB (byte 1) */
			uint16_t mode_reserved2; /**< Reserved bytes 2-3 */
		};
#endif
	};

#if defined(CONFIG_USERSPACE)
	uint32_t priv_stack_start; /**< Privileged mode stack start address */
	uint32_t priv_stack_end;   /**< Privileged mode stack end address */
#if defined(CONFIG_CPU_AARCH32_CORTEX_R)
	uint32_t sp_usr;           /**< User mode stack pointer for Cortex-R */
#endif
#endif
#endif

#if defined(CONFIG_ARM_PAC_PER_THREAD)
	struct pac_keys pac_keys;  /**< Per-thread PAC keys */
#endif
};

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_MPU_STACK_GUARD)
/** @brief Mask for MPU guard float mode bit in thread mode status */
#define Z_ARM_MODE_MPU_GUARD_FLOAT_Msk (1 << 3)
#endif

typedef struct _thread_arch _thread_arch_t;

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_ */
