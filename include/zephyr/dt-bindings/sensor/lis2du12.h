/**
 * @file
 * @brief ST LIS2DU12 accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DU12_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DU12_H_

/**
 * @defgroup lis2du12_dt_api ST LIS2DU12 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lis2du12_accel_range Accelerometer full-scale range
 * @{
 */
#define	LIS2DU12_DT_FS_2G			0 /**< ±2 g */
#define	LIS2DU12_DT_FS_4G			1 /**< ±4 g */
#define	LIS2DU12_DT_FS_8G			2 /**< ±8 g */
#define	LIS2DU12_DT_FS_16G			3 /**< ±16 g */
/** @} */

/**
 * @defgroup lis2du12_odr Output data rates
 * @{
 */
#define LIS2DU12_DT_ODR_OFF			0x00 /**< Power-down */
#define LIS2DU12_DT_ODR_AT_1Hz6_ULP		0x01 /**< 1.6 Hz (ultra low power) */
#define LIS2DU12_DT_ODR_AT_3Hz_ULP		0x02 /**< 3 Hz (ultra low power) */
#define LIS2DU12_DT_ODR_AT_6Hz_ULP		0x03 /**< 6 Hz (ultra low power) */
#define LIS2DU12_DT_ODR_AT_6Hz			0x04 /**< 6 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_12Hz			0x05 /**< 12.5 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_25Hz			0x06 /**< 25 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_50Hz			0x07 /**< 50 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_100Hz		0x08 /**< 100 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_200Hz		0x09 /**< 200 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_400Hz		0x0a /**< 400 Hz (normal mode) */
#define LIS2DU12_DT_ODR_AT_800Hz		0x0b /**< 800 Hz (normal mode) */
#define LIS2DU12_DT_ODR_TRIG_PIN		0x0e /**< Single-shot triggered by INT2 pin */
#define LIS2DU12_DT_ODR_TRIG_SW		0x0f /**< Single-shot triggered by interface */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DU12_H_ */
