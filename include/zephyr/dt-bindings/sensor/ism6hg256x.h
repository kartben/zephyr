/**
 * @file
 * @brief ST ISM6HG256X high-g IMU Devicetree constants
 */

/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_ISM6HG256X_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_ISM6HG256X_H_

#include "lsm6dsvxxx.h"

/**
 * @defgroup ism6hg256x_dt_api ST ISM6HG256X Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup ism6hg256x_hg_xl_odr High-g accelerometer output data rates
 * @{
 */
#define ISM6HG256X_HG_XL_ODR_OFF		0x0 /**< Power-down */
#define ISM6HG256X_HG_XL_ODR_AT_480Hz		0x3 /**< 480 Hz */
#define ISM6HG256X_HG_XL_ODR_AT_960Hz		0x4 /**< 960 Hz */
#define ISM6HG256X_HG_XL_ODR_AT_1920Hz		0x5 /**< 1920 Hz */
#define ISM6HG256X_HG_XL_ODR_AT_3840Hz		0x6 /**< 3840 Hz */
#define ISM6HG256X_HG_XL_ODR_AT_7680Hz		0x7 /**< 7680 Hz */
/** @} */

/**
 * @defgroup ism6hg256x_accel_range Accelerometer full-scale range
 * @{
 */
#define ISM6HG256X_DT_FS_2G			2   /**< ±2 g */
#define ISM6HG256X_DT_FS_4G			4   /**< ±4 g */
#define ISM6HG256X_DT_FS_8G			8   /**< ±8 g */
#define ISM6HG256X_DT_FS_16G			16  /**< ±16 g */
#define ISM6HG256X_DT_FS_32G			32  /**< ±32 g */
#define ISM6HG256X_DT_FS_64G			64  /**< ±64 g */
#define ISM6HG256X_DT_FS_128G			128 /**< ±128 g */
#define ISM6HG256X_DT_FS_256G			256 /**< ±256 g */
/** @} */

/**
 * @defgroup ism6hg256x_gyro_range Gyroscope full-scale range
 * @{
 */
#define ISM6HG256X_DT_FS_250DPS			0x1 /**< ±250 dps */
#define ISM6HG256X_DT_FS_500DPS			0x2 /**< ±500 dps */
#define ISM6HG256X_DT_FS_1000DPS		0x3 /**< ±1000 dps */
#define ISM6HG256X_DT_FS_2000DPS		0x4 /**< ±2000 dps */
#define ISM6HG256X_DT_FS_4000DPS		0x5 /**< ±4000 dps */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_ISM6HG256X_H_ */
