/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Renesas RX ICU (Interrupt Control Unit) driver API.
 * @ingroup misc_interfaces
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RX_ICU_H_
#define ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RX_ICU_H_

/** Digital filter sampling clock: PCLK */
#define IRQ_CFG_PCLK_DIV1  (0)
/** Digital filter sampling clock: PCLK / 8 */
#define IRQ_CFG_PCLK_DIV8  (1)
/** Digital filter sampling clock: PCLK / 32 */
#define IRQ_CFG_PCLK_DIV32 (2)
/** Digital filter sampling clock: PCLK / 64 */
#define IRQ_CFG_PCLK_DIV64 (3)

/** External interrupt pin detection modes. */
enum icu_irq_mode {
	ICU_LOW_LEVEL,  /**< Interrupt on low level */
	ICU_FALLING,    /**< Interrupt on falling edge */
	ICU_RISING,     /**< Interrupt on rising edge */
	ICU_BOTH_EDGE,  /**< Interrupt on both edges */
	ICU_MODE_NONE,  /**< No detection mode */
};

/** Digital filter enable settings. */
enum icu_dig_filt {
	DISENABLE_DIG_FILT, /**< Disable the digital filter */
	ENABLE_DIG_FILT,    /**< Enable the digital filter */
};

/** Digital filter settings for an external interrupt pin. */
typedef struct rx_irq_dig_filt_s {
	uint8_t filt_clk_div; /**< PCLK divisor setting for the input pin digital filter. */
	uint8_t filt_enable;  /**< Filter enable setting for the input pin digital filter. */
} rx_irq_dig_filt_t;

/**
 * @brief Clear the interrupt request (IR) flag of an interrupt.
 *
 * @param irqn Interrupt number.
 */
extern void rx_icu_clear_ir_flag(unsigned int irqn);

/**
 * @brief Get the interrupt request (IR) flag of an interrupt.
 *
 * @param irqn Interrupt number.
 *
 * @return 0 if no interrupt is requested, non-zero otherwise.
 */
extern int rx_icu_get_ir_flag(unsigned int irqn);

/**
 * @brief Set the detection mode of an external interrupt pin.
 *
 * @param pin_irqn External interrupt pin number.
 * @param mode Detection mode.
 *
 * @retval 0 If successful.
 * @retval -EINVAL If @p mode is not a valid detection mode.
 */
extern int rx_icu_set_irq_control(unsigned int pin_irqn, enum icu_irq_mode mode);

/**
 * @brief Configure the digital filter of an external interrupt pin.
 *
 * @param pin_irqn External interrupt pin number.
 * @param dig_filt Digital filter settings.
 */
extern void rx_icu_set_irq_dig_filt(unsigned int pin_irqn, rx_irq_dig_filt_t dig_filt);

#endif /* ZEPHYR_INCLUDE_DRIVERS_INTERRUPT_CONTROLLER_INTC_RX_ICU_H_ */
