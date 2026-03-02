/**
 * @file
 * @brief ST LIS2DW12 accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DW12_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DW12_H_

/**
 * @defgroup lis2dw12_dt_api ST LIS2DW12 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lis2dw12_power_modes Power modes
 * @{
 */
#define LIS2DW12_DT_LP_M1			0 /**< Low power mode 1 (12-bit resolution) */
#define LIS2DW12_DT_LP_M2			1 /**< Low power mode 2 (14-bit resolution) */
#define LIS2DW12_DT_LP_M3			2 /**< Low power mode 3 (14-bit resolution) */
#define LIS2DW12_DT_LP_M4			3 /**< Low power mode 4 (14-bit resolution) */
#define LIS2DW12_DT_HP_MODE			4 /**< High performance mode (14-bit resolution) */
/** @} */

/**
 * @defgroup lis2dw12_filter_bw Filter bandwidth
 * @{
 */
#define LIS2DW12_DT_FILTER_BW_ODR_DIV_2	0 /**< ODR/2 */
#define LIS2DW12_DT_FILTER_BW_ODR_DIV_4	1 /**< ODR/4 */
#define LIS2DW12_DT_FILTER_BW_ODR_DIV_10	2 /**< ODR/10 */
#define LIS2DW12_DT_FILTER_BW_ODR_DIV_20	3 /**< ODR/20 */
/** @} */

/**
 * @defgroup lis2dw12_tap_mode Tap detection mode
 * @{
 */
#define LIS2DW12_DT_SINGLE_TAP			0 /**< Single tap only */
#define LIS2DW12_DT_SINGLE_DOUBLE_TAP		1 /**< Single and double tap */
/** @} */

/**
 * @defgroup lis2dw12_freefall_threshold Free-fall threshold
 * @{
 */
#define LIS2DW12_DT_FF_THRESHOLD_156_mg		0 /**< 156 mg */
#define LIS2DW12_DT_FF_THRESHOLD_219_mg		1 /**< 219 mg */
#define LIS2DW12_DT_FF_THRESHOLD_250_mg		2 /**< 250 mg */
#define LIS2DW12_DT_FF_THRESHOLD_312_mg		3 /**< 312 mg */
#define LIS2DW12_DT_FF_THRESHOLD_344_mg		4 /**< 344 mg */
#define LIS2DW12_DT_FF_THRESHOLD_406_mg		5 /**< 406 mg */
#define LIS2DW12_DT_FF_THRESHOLD_469_mg		6 /**< 469 mg */
#define LIS2DW12_DT_FF_THRESHOLD_500_mg		7 /**< 500 mg */
/** @} */

/**
 * @defgroup lis2dw12_wakeup_duration Wake-up duration in ODR cycles
 * @{
 */
#define LIS2DW12_DT_WAKEUP_1_ODR		0 /**< 1 ODR cycle */
#define LIS2DW12_DT_WAKEUP_2_ODR		1 /**< 2 ODR cycles */
#define LIS2DW12_DT_WAKEUP_3_ODR		2 /**< 3 ODR cycles */
#define LIS2DW12_DT_WAKEUP_4_ODR		3 /**< 4 ODR cycles */
/** @} */

/**
 * @defgroup lis2dw12_sleep_duration Sleep duration in ODR cycles
 * @{
 */
#define LIS2DW12_DT_SLEEP_0_ODR		    0  /**< 0 ODR cycles */
#define LIS2DW12_DT_SLEEP_1_ODR		    1  /**< 1 ODR cycle */
#define LIS2DW12_DT_SLEEP_2_ODR		    2  /**< 2 ODR cycles */
#define LIS2DW12_DT_SLEEP_3_ODR		    3  /**< 3 ODR cycles */
#define LIS2DW12_DT_SLEEP_4_ODR		    4  /**< 4 ODR cycles */
#define LIS2DW12_DT_SLEEP_5_ODR		    5  /**< 5 ODR cycles */
#define LIS2DW12_DT_SLEEP_6_ODR		    6  /**< 6 ODR cycles */
#define LIS2DW12_DT_SLEEP_7_ODR		    7  /**< 7 ODR cycles */
#define LIS2DW12_DT_SLEEP_8_ODR		    8  /**< 8 ODR cycles */
#define LIS2DW12_DT_SLEEP_9_ODR		    9  /**< 9 ODR cycles */
#define LIS2DW12_DT_SLEEP_10_ODR		10 /**< 10 ODR cycles */
#define LIS2DW12_DT_SLEEP_11_ODR		11 /**< 11 ODR cycles */
#define LIS2DW12_DT_SLEEP_12_ODR		12 /**< 12 ODR cycles */
#define LIS2DW12_DT_SLEEP_13_ODR		13 /**< 13 ODR cycles */
#define LIS2DW12_DT_SLEEP_14_ODR		14 /**< 14 ODR cycles */
#define LIS2DW12_DT_SLEEP_15_ODR		15 /**< 15 ODR cycles */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DW12_H_ */
