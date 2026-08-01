/*
 * Copyright (c) 2025 Synopsys, Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the RISC-V IMSIC (Incoming MSI Controller) driver API and register definitions.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_RISCV_IMSIC_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_RISCV_IMSIC_H_

#include <zephyr/device.h>
#include <zephyr/types.h>
#include <zephyr/arch/riscv/csr.h>

/* IMSIC direct CSRs (M-mode) */
#define CSR_MTOPEI      0x35C /**< Machine top external interrupt CSR address */
#define CSR_MTOPI       0xFB0 /**< Machine top interrupt CSR address */
#define CSR_MISELECT    0x350 /**< Machine indirect register select CSR address */
#define CSR_MIREG       0x351 /**< Machine indirect register alias CSR address */
#define CSR_SETEIPNUM_M 0xFC0 /**< Write EIID to set pending bit */
#define CSR_CLREIPNUM_M 0xFC1 /**< Write EIID to clear pending bit */

/* MTOPEI register field masks */
#define MTOPEI_EIID_MASK  0x7FF /**< Bits [10:0]: External Interrupt ID (0-2047) */
#define MTOPEI_PRIO_SHIFT 16    /**< Bits [23:16]: Priority level */
#define MTOPEI_PRIO_MASK  (0xFF << MTOPEI_PRIO_SHIFT) /**< Priority level field mask */

/* IMSIC indirect CSR addresses (per privilege file) */
#define ICSR_EIDELIVERY 0x70 /**< External interrupt delivery enable register */
#define ICSR_EITHRESH   0x72 /**< External interrupt threshold register */
#define ICSR_EIP0       0x80 /**< External interrupt pending register 0 */
#define ICSR_EIP1       0x81 /**< External interrupt pending register 1 */
#define ICSR_EIP2       0x82 /**< External interrupt pending register 2 */
#define ICSR_EIP3       0x83 /**< External interrupt pending register 3 */
#define ICSR_EIP4       0x84 /**< External interrupt pending register 4 */
#define ICSR_EIP5       0x85 /**< External interrupt pending register 5 */
#define ICSR_EIP6       0x86 /**< External interrupt pending register 6 */
#define ICSR_EIP7       0x87 /**< External interrupt pending register 7 */
#define ICSR_EIE0       0xC0 /**< External interrupt enable register 0 */
#define ICSR_EIE1       0xC1 /**< External interrupt enable register 1 */
#define ICSR_EIE2       0xC2 /**< External interrupt enable register 2 */
#define ICSR_EIE3       0xC3 /**< External interrupt enable register 3 */
#define ICSR_EIE4       0xC4 /**< External interrupt enable register 4 */
#define ICSR_EIE5       0xC5 /**< External interrupt enable register 5 */
#define ICSR_EIE6       0xC6 /**< External interrupt enable register 6 */
#define ICSR_EIE7       0xC7 /**< External interrupt enable register 7 */

/** Enable external interrupt delivery */
#define EIDELIVERY_ENABLE    BIT(0)
/** Delivery mode value for MSI ("MMSI only": 00 = 0x00000000) */
#define EIDELIVERY_MODE_MMSI (0U << 29)

/* IMSIC API functions (implemented by drivers) */

/**
 * @brief Claim the top pending external interrupt on the current CPU's IMSIC
 *
 * Atomically reads the top external interrupt CSR and clears the pending
 * bit of the returned interrupt identity.
 *
 * @return EIID of the claimed interrupt, or 0 if no interrupt was pending
 */
uint32_t riscv_imsic_claim(void);

/**
 * @brief Enable an EIID in the CURRENT CPU's IMSIC
 *
 * This function uses CSR instructions that operate on the CPU executing
 * this code. To enable an EIID on a specific hart, this function MUST
 * be called from that hart (e.g., using k_thread_cpu_mask_enable).
 *
 * Following PLIC pattern: no parameter validation at API level.
 * Invalid EIIDs are caught in the ISR if they fire.
 *
 * @param eiid External Interrupt ID to enable (0-2047)
 */
void riscv_imsic_enable_eiid(uint32_t eiid);

/**
 * @brief Disable an EIID in the CURRENT CPU's IMSIC
 *
 * This function uses CSR instructions that operate on the CPU executing
 * this code. To disable an EIID on a specific hart, this function MUST
 * be called from that hart.
 *
 * Following PLIC pattern: no parameter validation at API level.
 * Invalid EIIDs are caught in the ISR if they fire.
 *
 * @param eiid External Interrupt ID to disable (0-2047)
 */
void riscv_imsic_disable_eiid(uint32_t eiid);

/**
 * @brief Check if an EIID is enabled in the CURRENT CPU's IMSIC
 *
 * @param eiid External Interrupt ID to check (0-2047)
 * @return 1 if enabled, 0 if disabled
 */
int riscv_imsic_is_enabled(uint32_t eiid);

#if defined(CONFIG_SMP)
/**
 * @brief Initialize IMSIC on secondary CPU
 *
 * Called during secondary CPU boot to configure that hart's IMSIC.
 * Configures EIDELIVERY, EITHRESHOLD, and enables MEXT interrupt.
 */
void z_riscv_imsic_secondary_init(void);
#endif /* CONFIG_SMP */

#endif
