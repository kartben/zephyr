/*
 * Copyright (c) 2023 Marek Vedral <vedrama5@fel.cvut.cz>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief ARM AArch32 GDB stub definitions
 *
 * This header provides definitions and structures for the ARM AArch32
 * GDB stub implementation. It defines register indices, debug-related
 * constants, and the GDB context structure used for remote debugging.
 */

#ifndef ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_GDBSTUB_H_
#define ZEPHYR_INCLUDE_ARCH_ARM_AARCH32_GDBSTUB_H_

#include <zephyr/arch/exception.h>

#ifndef _ASMLANGUAGE

/** @brief Debug Status and Control Register - Monitor mode enable bit */
#define DBGDSCR_MONITOR_MODE_EN 0x8000

/** @brief SPSR instruction set state for ARM mode */
#define SPSR_ISETSTATE_ARM     0x0
/** @brief SPSR instruction set state for Jazelle mode */
#define SPSR_ISETSTATE_JAZELLE 0x2
/** @brief SPSR J (Jazelle) bit position */
#define SPSR_J                 24
/** @brief SPSR T (Thumb) bit position */
#define SPSR_T                 5

/* Debug Breakpoint Control Register constants */
/** @brief DBGDBCR meaning field mask */
#define DBGDBCR_MEANING_MASK          0x7
/** @brief DBGDBCR meaning field shift */
#define DBGDBCR_MEANING_SHIFT         20
/** @brief DBGDBCR address mismatch meaning value */
#define DBGDBCR_MEANING_ADDR_MISMATCH 0x4
/** @brief DBGDBCR byte address mask */
#define DBGDBCR_BYTE_ADDR_MASK        0xF
/** @brief DBGDBCR byte address shift */
#define DBGDBCR_BYTE_ADDR_SHIFT       5
/** @brief DBGDBCR breakpoint enable mask */
#define DBGDBCR_BRK_EN_MASK           0x1

/** @brief Register number of the SPSR in the packet_pos array */
#define SPSR_REG_IDX    25
/** @brief Minimal size of the GDB read all registers packet (42 bytes * 8) */
#define GDB_READALL_PACKET_SIZE (42 * 8)

/** @brief IFSR debug event code */
#define IFSR_DEBUG_EVENT 0x2

/**
 * @brief ARM AArch32 GDB register indices
 *
 * Enumeration of register indices used for GDB protocol communication.
 * These correspond to the ARM general-purpose registers and special
 * registers as defined in the GDB remote protocol for ARM targets.
 */
enum AARCH32_GDB_REG {
	R0 = 0,  /**< General purpose register R0 */
	R1,      /**< General purpose register R1 */
	R2,      /**< General purpose register R2 */
	R3,      /**< General purpose register R3 */
	/* READONLY registers (R4 - R13) except R12 */
	R4,      /**< General purpose register R4 (callee-saved) */
	R5,      /**< General purpose register R5 (callee-saved) */
	R6,      /**< General purpose register R6 (callee-saved) */
	R7,      /**< General purpose register R7 (callee-saved) */
	R8,      /**< General purpose register R8 (callee-saved) */
	R9,      /**< General purpose register R9 (callee-saved) */
	R10,     /**< General purpose register R10 (callee-saved) */
	R11,     /**< General purpose register R11 (frame pointer) */
	R12,     /**< General purpose register R12 (IP) */
	/* Stack pointer - READONLY */
	R13,     /**< Stack pointer register R13 (SP) */
	LR,      /**< Link register R14 */
	PC,      /**< Program counter R15 */
	/* Saved program status register */
	SPSR,    /**< Saved program status register */
	GDB_NUM_REGS /**< Total number of GDB-accessible registers */
};

/**
 * @brief GDB context structure for ARM AArch32
 *
 * This structure holds the debugging context including the exception
 * cause and register values. It is populated when entering the GDB
 * stub and used for register inspection and modification.
 */
struct gdb_ctx {
	unsigned int exception;              /**< Cause of the exception */
	unsigned int registers[GDB_NUM_REGS]; /**< Register values */
};

/**
 * @brief GDB stub entry point
 *
 * This function is called when a debug exception occurs. It saves the
 * exception context and enters the GDB main loop for remote debugging.
 *
 * @param esf Pointer to the exception stack frame.
 * @param exc_cause The cause of the exception.
 */
void z_gdb_entry(struct arch_esf *esf, unsigned int exc_cause);

#endif

#endif
