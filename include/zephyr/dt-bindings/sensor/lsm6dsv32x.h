/**
 * @file
 * @brief ST LSM6DSV32X sensor Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSV32X_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSV32X_H_

#include "lsm6dsvxxx.h"

/**
 * @defgroup lsm6dsv32x_dt_api ST LSM6DSV32X Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lsm6dsv32x_accel_range Accelerometer full-scale range
 * @{
 */
#define LSM6DSV32X_DT_FS_4G                     4  /**< ±4 g */
#define LSM6DSV32X_DT_FS_8G                     8  /**< ±8 g */
#define LSM6DSV32X_DT_FS_16G                    16 /**< ±16 g */
#define LSM6DSV32X_DT_FS_32G                    32 /**< ±32 g */
/** @} */

/**
 * @defgroup lsm6dsv32x_gyro_range Gyroscope full-scale range
 * @{
 */
#define LSM6DSV32X_DT_FS_125DPS			0x0 /**< ±125 dps */
#define LSM6DSV32X_DT_FS_250DPS			0x1 /**< ±250 dps */
#define LSM6DSV32X_DT_FS_500DPS			0x2 /**< ±500 dps */
#define LSM6DSV32X_DT_FS_1000DPS		0x3 /**< ±1000 dps */
#define LSM6DSV32X_DT_FS_2000DPS		0x4 /**< ±2000 dps */
#define LSM6DSV32X_DT_FS_4000DPS		0xc /**< ±4000 dps */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSV32X_H_ */
