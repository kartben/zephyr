/*
 * Copyright (c) 2021 ITE Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for ITE IT8XXX2
 * @ingroup pwm_it8xxx2
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT8XXX2_PWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT8XXX2_PWM_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @addtogroup ite_pwm ITE PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_it8xxx2 ITE IT8XXX2 PWM helpers
 * @brief Macros for PWM configuration of ITE IT8XXX2
 * @ingroup ite_pwm
 * @{
 */

/**
 * @name IT8XXX2 PWM prescaler references
 * @{
 */
/** Prescaler reference C4. */
#define PWM_PRESCALER_C4	1
/** Prescaler reference C6. */
#define PWM_PRESCALER_C6	2
/** Prescaler reference C7. */
#define PWM_PRESCALER_C7	3
/** @} */

/**
 * @name IT8XXX2 PWM channel identifiers
 * @{
 */
/** PWM channel 0. */
#define PWM_CHANNEL_0		0
/** PWM channel 1. */
#define PWM_CHANNEL_1		1
/** PWM channel 2. */
#define PWM_CHANNEL_2		2
/** PWM channel 3. */
#define PWM_CHANNEL_3		3
/** PWM channel 4. */
#define PWM_CHANNEL_4		4
/** PWM channel 5. */
#define PWM_CHANNEL_5		5
/** PWM channel 6. */
#define PWM_CHANNEL_6		6
/** PWM channel 7. */
#define PWM_CHANNEL_7		7
/** @} */

/**
 * @name IT8XXX2 PWM open-drain flags
 * SoC-specific configuration flags. The upper 8 bits of pwm_flags_t are
 * reserved for SoC-specific use; the open-drain flag occupies bit 8.
 * @{
 */
/** Configure the PWM output as open-drain. */
#define PWM_IT8XXX2_OPEN_DRAIN	BIT(8)
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_IT8XXX2_PWM_H_ */
