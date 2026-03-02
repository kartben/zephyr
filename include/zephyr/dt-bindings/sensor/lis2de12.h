/**
 * @file
 * @brief ST LIS2DE12 accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2024 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DE12_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DE12_H_

/**
 * @defgroup lis2de12_dt_api ST LIS2DE12 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lis2de12_accel_range Accelerometer full-scale range
 * @{
 */
#define	LIS2DE12_DT_FS_2G			0 /**< ±2 g */
#define	LIS2DE12_DT_FS_4G			1 /**< ±4 g */
#define	LIS2DE12_DT_FS_8G			2 /**< ±8 g */
#define	LIS2DE12_DT_FS_16G			3 /**< ±16 g */
/** @} */

/**
 * @defgroup lis2de12_odr Output data rates
 * @{
 */
#define LIS2DE12_DT_ODR_OFF			0x00 /**< Power-down */
#define LIS2DE12_DT_ODR_AT_1Hz			0x01 /**< 1 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_10Hz		0x02 /**< 10 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_25Hz		0x03 /**< 25 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_50Hz		0x04 /**< 50 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_100Hz		0x05 /**< 100 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_200Hz		0x06 /**< 200 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_400Hz		0x07 /**< 400 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_1kHz620		0x08 /**< 1620 Hz (normal mode) */
#define LIS2DE12_DT_ODR_AT_5kHz376		0x09 /**< 5376 Hz (normal mode) */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DE12_H_ */
