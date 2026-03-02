/**
 * @file
 * @brief ST IIS2DLPC accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_IIS2DLPC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_IIS2DLPC_H_

/**
 * @defgroup iis2dlpc_dt_api ST IIS2DLPC Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup iis2dlpc_power_modes Power modes
 * @{
 */
#define IIS2DLPC_DT_LP_M1			0 /**< Low power mode 1 (12-bit resolution) */
#define IIS2DLPC_DT_LP_M2			1 /**< Low power mode 2 (14-bit resolution) */
#define IIS2DLPC_DT_LP_M3			2 /**< Low power mode 3 (14-bit resolution) */
#define IIS2DLPC_DT_LP_M4			3 /**< Low power mode 4 (14-bit resolution) */
#define IIS2DLPC_DT_HP_MODE			4 /**< High performance mode (14-bit resolution) */
/** @} */

/**
 * @defgroup iis2dlpc_filter_bw Filter bandwidth
 * @{
 */
#define IIS2DLPC_DT_FILTER_BW_ODR_DIV_2		0 /**< ODR/2 */
#define IIS2DLPC_DT_FILTER_BW_ODR_DIV_4		1 /**< ODR/4 */
#define IIS2DLPC_DT_FILTER_BW_ODR_DIV_10 	2 /**< ODR/10 */
#define IIS2DLPC_DT_FILTER_BW_ODR_DIV_20 	3 /**< ODR/20 */
/** @} */

/**
 * @defgroup iis2dlpc_tap_mode Tap detection mode
 * @{
 */
#define IIS2DLPC_DT_SINGLE_TAP			0 /**< Single tap only */
#define IIS2DLPC_DT_SINGLE_DOUBLE_TAP		1 /**< Single and double tap */
/** @} */

/**
 * @defgroup iis2dlpc_freefall_threshold Free-fall threshold
 * @{
 */
#define IIS2DLPC_DT_FF_THRESHOLD_156_mg		0 /**< 156 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_219_mg		1 /**< 219 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_250_mg		2 /**< 250 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_312_mg		3 /**< 312 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_344_mg		4 /**< 344 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_406_mg		5 /**< 406 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_469_mg		6 /**< 469 mg */
#define IIS2DLPC_DT_FF_THRESHOLD_500_mg		7 /**< 500 mg */
/** @} */

/**
 * @defgroup iis2dlpc_wakeup_duration Wake-up duration in ODR cycles
 * @{
 */
#define IIS2DLPC_DT_WAKEUP_1_ODR		0 /**< 1 ODR cycle */
#define IIS2DLPC_DT_WAKEUP_2_ODR		1 /**< 2 ODR cycles */
#define IIS2DLPC_DT_WAKEUP_3_ODR		2 /**< 3 ODR cycles */
#define IIS2DLPC_DT_WAKEUP_4_ODR		3 /**< 4 ODR cycles */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_IIS2DLPC_H_ */
