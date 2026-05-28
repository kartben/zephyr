/*
 * Copyright (c) 2025 ITE Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for ITE IT51XXX
 * @ingroup pwm_it51xxx
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT51XXX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT51XXX_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @addtogroup ite_pwm ITE PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_it51xxx ITE IT51XXX PWM helpers
 * @brief Macros for PWM configuration of ITE IT51XXX
 * @ingroup ite_pwm
 * @{
 */

/**
 * @name IT51XXX PWM prescaler references
 * @{
 */
#define PWM_PRESCALER_C4	1
#define PWM_PRESCALER_C6	2
#define PWM_PRESCALER_C7	3
/** @} */

/**
 * @name IT51XXX PWM channel identifiers
 * @{
 */
#define PWM_CHANNEL_0		0
#define PWM_CHANNEL_1		1
#define PWM_CHANNEL_2		2
#define PWM_CHANNEL_3		3
#define PWM_CHANNEL_4		4
#define PWM_CHANNEL_5		5
#define PWM_CHANNEL_6		6
#define PWM_CHANNEL_7		7
/** @} */

/**
 * @name IT51XXX PWM open-drain flags
 * SoC-specific configuration flags. The upper 8 bits of pwm_flags_t are
 * reserved for SoC-specific use; the open-drain flag occupies bit 8.
 * @{
 */
/** Configure the PWM output as open-drain. */
#define PWM_IT51XXX_OPEN_DRAIN	BIT(8)
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT51XXX_H_ */
