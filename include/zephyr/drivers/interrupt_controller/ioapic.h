/* ioapic.h - public IOAPIC APIs */

/*
 * Copyright (c) 2012-2015 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Intel IOAPIC interrupt controller driver API.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_IOAPIC_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_IOAPIC_H_

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Redirection table entry bits: lower 32 bit
 * Used as flags argument in ioapic_irq_set
 */

#define IOAPIC_INT_MASK 0x00010000 /**< Interrupt mask bit */
#define IOAPIC_TRIGGER_MASK 0x00008000 /**< Trigger mode field mask */
#define IOAPIC_LEVEL 0x00008000 /**< Level triggered mode */
#define IOAPIC_EDGE 0x00000000 /**< Edge triggered mode */
#define IOAPIC_REMOTE 0x00004000 /**< Remote IRR bit */
#define IOAPIC_POLARITY_MASK 0x00002000 /**< Input pin polarity field mask */
#define IOAPIC_LOW 0x00002000 /**< Active low polarity */
#define IOAPIC_HIGH 0x00000000 /**< Active high polarity */
#define IOAPIC_LOGICAL 0x00000800 /**< Logical destination mode */
#define IOAPIC_PHYSICAL 0x00000000 /**< Physical destination mode */
#define IOAPIC_DELIVERY_MODE_MASK 0x00000700 /**< Delivery mode field mask */
#define IOAPIC_FIXED 0x00000000 /**< Fixed delivery mode */
#define IOAPIC_LOWEST 0x00000100 /**< Lowest priority delivery mode */
#define IOAPIC_SMI 0x00000200 /**< SMI delivery mode */
#define IOAPIC_NMI 0x00000400 /**< NMI delivery mode */
#define IOAPIC_INIT 0x00000500 /**< INIT delivery mode */
#define IOAPIC_EXTINT 0x00000700 /**< ExtINT delivery mode */

#ifndef _ASMLANGUAGE
uint32_t z_ioapic_num_rtes(void);
void z_ioapic_irq_enable(unsigned int irq);
void z_ioapic_irq_disable(unsigned int irq);
void z_ioapic_int_vec_set(unsigned int irq, unsigned int vector);
void z_ioapic_irq_set(unsigned int irq, unsigned int vector, uint32_t flags);
#endif /* _ASMLANGUAGE */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_IOAPIC_H_ */
