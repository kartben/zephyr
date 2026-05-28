/*
 * Copyright (c) 2024 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree PWM helpers for Renesas RA
 * @ingroup pwm_ra
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RA_PWM_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RA_PWM_H_

/**
 * @addtogroup renesas_pwm Renesas PWM helpers
 * @ingroup devicetree-pwms
 */

/**
 * @defgroup pwm_ra Renesas RA PWM helpers
 * @brief Macros for PWM configuration of Renesas RA
 * @ingroup renesas_pwm
 * @{
 */

/**
 * @name RA PWM source divider values
 * Selects the PWM source clock divider.
 * @{
 */
/** PWM source clock divided by 1. */
#define RA_PWM_SOURCE_DIV_1    0
/** PWM source clock divided by 2. */
#define RA_PWM_SOURCE_DIV_2    1
/** PWM source clock divided by 4. */
#define RA_PWM_SOURCE_DIV_4    2
/** PWM source clock divided by 8. */
#define RA_PWM_SOURCE_DIV_8    3
/** PWM source clock divided by 16. */
#define RA_PWM_SOURCE_DIV_16   4
/** PWM source clock divided by 32. */
#define RA_PWM_SOURCE_DIV_32   5
/** PWM source clock divided by 64. */
#define RA_PWM_SOURCE_DIV_64   6
/** PWM source clock divided by 128. */
#define RA_PWM_SOURCE_DIV_128  7
/** PWM source clock divided by 256. */
#define RA_PWM_SOURCE_DIV_256  8
/** PWM source clock divided by 512. */
#define RA_PWM_SOURCE_DIV_512  9
/** PWM source clock divided by 1024. */
#define RA_PWM_SOURCE_DIV_1024 10
/** @} */

/**
 * @name RA PWM channel identifiers
 * @{
 */
/** RA PWM channel 0. */
#define RA_PWM_CHANNEL_0  0
/** RA PWM channel 1. */
#define RA_PWM_CHANNEL_1  1
/** RA PWM channel 2. */
#define RA_PWM_CHANNEL_2  2
/** RA PWM channel 3. */
#define RA_PWM_CHANNEL_3  3
/** RA PWM channel 4. */
#define RA_PWM_CHANNEL_4  4
/** RA PWM channel 5. */
#define RA_PWM_CHANNEL_5  5
/** RA PWM channel 6. */
#define RA_PWM_CHANNEL_6  6
/** RA PWM channel 7. */
#define RA_PWM_CHANNEL_7  7
/** RA PWM channel 8. */
#define RA_PWM_CHANNEL_8  8
/** RA PWM channel 9. */
#define RA_PWM_CHANNEL_9  9
/** RA PWM channel 10. */
#define RA_PWM_CHANNEL_10 10
/** RA PWM channel 11. */
#define RA_PWM_CHANNEL_11 11
/** RA PWM channel 12. */
#define RA_PWM_CHANNEL_12 12
/** RA PWM channel 13. */
#define RA_PWM_CHANNEL_13 13
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PWM_RA_PWM_H_ */
