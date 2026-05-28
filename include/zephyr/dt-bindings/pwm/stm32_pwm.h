/*
 * Copyright (c) 2022 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for STMicroelectronics STM32
 * @ingroup pwm_stm32
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_STM32_PWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_STM32_PWM_H_

/**
 * @addtogroup st_pwm STMicroelectronics PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_stm32 STMicroelectronics STM32 PWM helpers
 * @brief Macros for PWM configuration of STMicroelectronics STM32
 * @ingroup st_pwm
 * @{
 */

/** PWM complementary output pin is enabled.
 *
 * This flag can be used with any `pwm_pin_set_*` API call to indicate
 * that the PWM signal must be routed to the complementary output channel.
 * This feature is only available on certain SoC families; refer to the
 * binding's documentation for more details.
 * The custom flags are in the upper 8 bits of `pwm_flags_t`.
 */
#define STM32_PWM_COMPLEMENTARY	(1U << 8)

/** @cond INTERNAL_HIDDEN */
#define STM32_PWM_COMPLEMENTARY_MASK	0x100
/** @endcond */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_STM32_PWM_H_ */
