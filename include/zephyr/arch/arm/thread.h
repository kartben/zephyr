/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright 2025 Arm Limited and/or its affiliates <open-source-office@arm.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Per-arch thread definition
 *
 * This file contains definitions for
 *
 *  struct _thread_arch
 *  struct _callee_saved
  *
 * necessary to instantiate instances of struct k_thread.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

/**
 * @brief ARM callee-saved register context
 *
 * Structure containing the callee-saved registers that must be preserved
 * across function calls and context switches according to the ARM AAPCS.
 */
struct _callee_saved {
	uint32_t v1;  /**< General purpose register r4 */
	uint32_t v2;  /**< General purpose register r5 */
	uint32_t v3;  /**< General purpose register r6 */
	uint32_t v4;  /**< General purpose register r7 */
	uint32_t v5;  /**< General purpose register r8 */
	uint32_t v6;  /**< General purpose register r9 */
	uint32_t v7;  /**< General purpose register r10 */
	uint32_t v8;  /**< General purpose register r11 */
	uint32_t psp; /**< Process stack pointer r13 */
#ifdef CONFIG_USE_SWITCH
	uint32_t lr;  /**< Link register (return address) */
#endif
};

typedef struct _callee_saved _callee_saved_t;

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
/**
 * @brief ARM FPU preemptible floating-point register context
 *
 * Structure containing floating-point registers s16-s31 that are not
 * automatically saved by the Cortex-M hardware and must be preserved
 * when preempting a thread using the FPU.
 */
struct _preempt_float {
	float  s16;  /**< FPU register s16 */
	float  s17;  /**< FPU register s17 */
	float  s18;  /**< FPU register s18 */
	float  s19;  /**< FPU register s19 */
	float  s20;  /**< FPU register s20 */
	float  s21;  /**< FPU register s21 */
	float  s22;  /**< FPU register s22 */
	float  s23;  /**< FPU register s23 */
	float  s24;  /**< FPU register s24 */
	float  s25;  /**< FPU register s25 */
	float  s26;  /**< FPU register s26 */
	float  s27;  /**< FPU register s27 */
	float  s28;  /**< FPU register s28 */
	float  s29;  /**< FPU register s29 */
	float  s30;  /**< FPU register s30 */
	float  s31;  /**< FPU register s31 */
};
#endif

#if defined(CONFIG_ARM_PAC_PER_THREAD)
/**
 * @brief ARM Pointer Authentication Code (PAC) keys
 *
 * Structure containing per-thread PAC keys used for pointer authentication
 * on ARM processors with PAC support.
 */
struct pac_keys {
	uint32_t key_0;  /**< PAC key 0 */
	uint32_t key_1;  /**< PAC key 1 */
	uint32_t key_2;  /**< PAC key 2 */
	uint32_t key_3;  /**< PAC key 3 */
};
#endif

/**
 * @brief ARM thread architecture-specific state
 *
 * Architecture-specific thread state for ARM processors. This structure
 * contains interrupt locking state, context switch values, FPU context,
 * exception depth tracking, privilege mode status, and userspace stack
 * information.
 */
struct _thread_arch {

	uint32_t basepri;  /**< Interrupt locking key (BASEPRI value) */

	uint32_t swap_return_value;  /**< Return value for context switches (r0 cannot be written reliably) */

#if defined(CONFIG_FPU) && defined(CONFIG_FPU_SHARING)
	/*
	 * No cooperative floating point register set structure exists for
	 * the Cortex-M as it automatically saves the necessary registers
	 * in its exception stack frame.
	 */
	struct _preempt_float  preempt_float;  /**< Preemptible FPU register context */
#endif

#if defined(CONFIG_CPU_AARCH32_CORTEX_A) || defined(CONFIG_CPU_AARCH32_CORTEX_R)
	int8_t exception_depth;  /**< Current exception nesting depth */
#endif

#if defined(CONFIG_ARM_STORE_EXC_RETURN) || defined(CONFIG_USERSPACE)
	/*
	 * Status variable holding several thread status flags
	 * as follows:
	 *
	 * byte 0
	 * +-bits 4-7-----bit-3----------bit-2--------bit-1---+----bit-0------+
	 * :          |             |              |          |               |
	 * : reserved |<Guard FLOAT>|   reserved   | reserved |  <priv mode>  |
	 * :   bits   |             |              |          | CONTROL.nPRIV |
	 * +------------------------------------------------------------------+
	 *
	 * byte 1
	 * +----------------------------bits 8-15-----------------------------+
	 * :              Least significant byte of EXC_RETURN                |
	 * : bit 15| bit 14| bit 13 | bit 12| bit 11 | bit 10 | bit 9 | bit 8 |
	 * :  Res  |   S   |  DCRS  | FType |  Mode  | SPSel  |  Res  |  ES   |
	 * +------------------------------------------------------------------+
	 *
	 * Bit 0: thread's current privileged mode (Supervisor or User mode)
	 *        Mirrors CONTROL.nPRIV flag.
	 * Bit 2: Deprecated in favor of FType. Note: FType = !CONTROL.FPCA.
	 *        indicating whether the thread has an active FP context.
	 *        Mirrors CONTROL.FPCA flag.
	 * Bit 3: indicating whether the thread is applying the long (FLOAT)
	 *        or the default MPU stack guard size.
	 *
	 * Bits 8-15: Least significant octet of the EXC_RETURN value when a
	 *            thread is switched-out. The value is copied from LR when
	 *            entering the PendSV handler. When the thread is
	 *            switched in again, the value is restored to LR before
	 *            exiting the PendSV handler.
	 */
	/** @brief Thread mode and exception return state */
	union {
		uint32_t mode;  /**< Combined mode field */

#if defined(CONFIG_ARM_STORE_EXC_RETURN)
		struct {
			uint8_t mode_bits;        /**< Privilege and FPU mode bits */
			uint8_t mode_exc_return;  /**< EXC_RETURN value (LSB) */
			uint16_t mode_reserved2;  /**< Reserved for future use */
		};
#endif
	};

#if defined(CONFIG_USERSPACE)
	uint32_t priv_stack_start;  /**< Start of privileged mode stack */
	uint32_t priv_stack_end;    /**< End of privileged mode stack */
#if defined(CONFIG_CPU_AARCH32_CORTEX_R)
	uint32_t sp_usr;  /**< User mode stack pointer (Cortex-R only) */
#endif
#endif
#endif

#if defined(CONFIG_ARM_PAC_PER_THREAD)
	struct pac_keys pac_keys;  /**< Per-thread pointer authentication keys */
#endif
};

#if defined(CONFIG_FPU_SHARING) && defined(CONFIG_MPU_STACK_GUARD)
#define Z_ARM_MODE_MPU_GUARD_FLOAT_Msk (1 << 3)
#endif
typedef struct _thread_arch _thread_arch_t;

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_ARCH_ARM_THREAD_H_ */
