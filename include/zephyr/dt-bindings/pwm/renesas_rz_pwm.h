/*
 * Copyright (c) 2024-2025 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for Renesas RZ
 * @ingroup pwm_renesas_rz
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_RENESAS_RZ_PWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_RENESAS_RZ_PWM_H_

/**
 * @addtogroup renesas_pwm Renesas PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_renesas_rz Renesas RZ PWM helpers
 * @brief Macros for PWM configuration of Renesas RZ
 * @ingroup renesas_pwm
 * @{
 */

/**
 * @name RZ PWM GPT IO selectors
 * @{
 */
/** GPT IO line A. */
#define RZ_PWM_GPT_IO_A 0
/** GPT IO line B. */
#define RZ_PWM_GPT_IO_B 1
/** @} */

/**
 * @name RZ PWM MTU IO selectors
 * @{
 */
/** MTU IO line A. */
#define RZ_PWM_MTIOCxA 0
/** MTU IO line B. */
#define RZ_PWM_MTIOCxB 1
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_RENESAS_RZ_PWM_H_ */
