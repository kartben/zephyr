/*
 * Copyright (c) 2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for Renesas RX MTU
 * @ingroup pwm_rx_mtu
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RX_MTU_PWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RX_MTU_PWM_H_

/**
 * @addtogroup renesas_pwm Renesas PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_rx_mtu Renesas RX MTU PWM helpers
 * @brief Macros for PWM configuration of Renesas RX MTU
 * @ingroup renesas_pwm
 * @{
 */

/**
 * @name RX MTU PWM source divider values
 * Selects the PWM source clock divider.
 * @{
 */
/** PWM source clock divided by 1. */
#define RX_MTU_PWM_SOURCE_DIV_1  0
/** PWM source clock divided by 4. */
#define RX_MTU_PWM_SOURCE_DIV_4  1
/** PWM source clock divided by 16. */
#define RX_MTU_PWM_SOURCE_DIV_16 2
/** PWM source clock divided by 64. */
#define RX_MTU_PWM_SOURCE_DIV_64 3
/** @} */

/**
 * @name RX MTU PWM IO selectors
 * Selects the MTU input/output line associated with a PWM channel.
 * @{
 */
/** MTU IO line A. */
#define RX_MTIOCxA 0
/** MTU IO line B. */
#define RX_MTIOCxB 1
/** MTU IO line C. */
#define RX_MTIOCxC 2
/** MTU IO line D. */
#define RX_MTIOCxD 3
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RX_MTU_PWM_H_ */
