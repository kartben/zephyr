/*
 * SPDX-FileCopyrightText: <text>Copyright (c) 2026 Infineon Technologies AG,
 * or an affiliate of Infineon Technologies AG. All rights reserved.</text>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for Infineon TCPWM
 * @ingroup pwm_ifx_tcpwm
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_PWM_IFX_TCPWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_PWM_IFX_TCPWM_H_

/**
 * @addtogroup infineon_pwm Infineon PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_ifx_tcpwm Infineon TCPWM PWM helpers
 * @brief Macros for PWM configuration of Infineon TCPWM
 * @ingroup infineon_pwm
 * @{
 */

/**
 * @name Infineon TCPWM divider type
 * Selects the width of the system-clock divider stage used by the TCPWM
 * peripheral.
 * @{
 */
/** 8-bit system-clock divider. */
#define PWM_IFX_SYSCLK_DIV_8_BIT  0
/** 16-bit system-clock divider. */
#define PWM_IFX_SYSCLK_DIV_16_BIT 1
/** @} */

/**
 * @name Infineon TCPWM output state flags
 * Custom PWM flags for Infineon TCPWM. These flags can be used with the PWM
 * API in the upper 8 bits of pwm_flags_t. They allow configuring the output
 * behavior of the PWM pins when the PWM is disabled.
 * @{
 */
/** Output is placed in high-impedance state. */
#define PWM_IFX_TCPWM_OUTPUT_HIGHZ  0
/** Output retains its last level. */
#define PWM_IFX_TCPWM_OUTPUT_RETAIN 1
/** Output is driven low. */
#define PWM_IFX_TCPWM_OUTPUT_LOW    2
/** Output is driven high. */
#define PWM_IFX_TCPWM_OUTPUT_HIGH   3

/** Mask covering the TCPWM output-state field. */
#define PWM_IFX_TCPWM_OUTPUT_MASK 0x3
/** Bit position of the TCPWM output-state field within pwm_flags_t. */
#define PWM_IFX_TCPWM_OUTPUT_POS  8 /* Place at the end of flags */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_PWM_IFX_TCPWM_H_ */
