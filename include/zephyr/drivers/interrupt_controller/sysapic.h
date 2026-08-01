/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the x86 system APIC interrupt controller interface.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_SYSAPIC_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_SYSAPIC_H_

#include <zephyr/drivers/interrupt_controller/loapic.h>

#define IRQ_TRIGGER_EDGE	IOAPIC_EDGE  /**< Edge triggered mode */
#define IRQ_TRIGGER_LEVEL	IOAPIC_LEVEL /**< Level triggered mode */

#define IRQ_POLARITY_HIGH	IOAPIC_HIGH  /**< Active high polarity */
#define IRQ_POLARITY_LOW	IOAPIC_LOW   /**< Active low polarity */

#ifndef _ASMLANGUAGE
#include <zephyr/types.h>

/** Number of local APIC IRQ lines (default to LOAPIC_TIMER to LOAPIC_ERROR) */
#define LOAPIC_IRQ_COUNT 6

void z_irq_controller_irq_config(unsigned int vector, unsigned int irq,
				 uint32_t flags);

int z_irq_controller_isr_vector_get(void);

static inline void z_irq_controller_eoi(void)
{
	x86_write_loapic(LOAPIC_EOI, 0);
}

#endif /* _ASMLANGUAGE */

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_SYSAPIC_H_ */
