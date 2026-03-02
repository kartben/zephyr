/**
 * @file
 * @brief ST LSM9DS1 IMU Devicetree constants
 */

/*
 * Copyright (c) 2024 Bootlin
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM9DS1_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM9DS1_H_

/**
 * @defgroup lsm9ds1_dt_api ST LSM9DS1 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lsm9ds1_accel_range Accelerometer full-scale range
 * @{
 */
#define LSM9DS1_DT_FS_2G  0 /**< ±2 g */
#define LSM9DS1_DT_FS_16G 1 /**< ±16 g */
#define LSM9DS1_DT_FS_4G  2 /**< ±4 g */
#define LSM9DS1_DT_FS_8G  3 /**< ±8 g */
/** @} */

/**
 * @defgroup lsm9ds1_gyro_range Gyroscope full-scale range
 * @{
 */
#define LSM9DS1_DT_FS_245DPS  0 /**< ±245 dps */
#define LSM9DS1_DT_FS_500DPS  1 /**< ±500 dps */
#define LSM9DS1_DT_FS_2000DPS 3 /**< ±2000 dps */
/** @} */

/**
 * @defgroup lsm9ds1_imu_odr IMU (accel + gyro) output data rates
 *
 * The encoded value combines gyro ODR (MSN) and accel ODR (LSN).
 * @{
 */
#define LSM9DS1_IMU_OFF            0x00 /**< Both powered down */
#define LSM9DS1_GY_OFF_XL_10Hz    0x10 /**< Gyro off, accel 10 Hz */
#define LSM9DS1_GY_OFF_XL_50Hz    0x20 /**< Gyro off, accel 50 Hz */
#define LSM9DS1_GY_OFF_XL_119Hz   0x30 /**< Gyro off, accel 119 Hz */
#define LSM9DS1_GY_OFF_XL_238Hz   0x40 /**< Gyro off, accel 238 Hz */
#define LSM9DS1_GY_OFF_XL_476Hz   0x50 /**< Gyro off, accel 476 Hz */
#define LSM9DS1_GY_OFF_XL_952Hz   0x60 /**< Gyro off, accel 952 Hz */
#define LSM9DS1_XL_OFF_GY_14Hz9   0x01 /**< Accel off, gyro 14.9 Hz */
#define LSM9DS1_XL_OFF_GY_59Hz5   0x02 /**< Accel off, gyro 59.5 Hz */
#define LSM9DS1_XL_OFF_GY_119Hz   0x03 /**< Accel off, gyro 119 Hz */
#define LSM9DS1_XL_OFF_GY_238Hz   0x04 /**< Accel off, gyro 238 Hz */
#define LSM9DS1_XL_OFF_GY_476Hz   0x05 /**< Accel off, gyro 476 Hz */
#define LSM9DS1_XL_OFF_GY_952Hz   0x06 /**< Accel off, gyro 952 Hz */
#define LSM9DS1_IMU_14Hz9         0x11 /**< Both at 14.9 Hz */
#define LSM9DS1_IMU_59Hz5         0x22 /**< Both at 59.5 Hz */
#define LSM9DS1_IMU_119Hz         0x33 /**< Both at 119 Hz */
#define LSM9DS1_IMU_238Hz         0x44 /**< Both at 238 Hz */
#define LSM9DS1_IMU_476Hz         0x55 /**< Both at 476 Hz */
#define LSM9DS1_IMU_952Hz         0x66 /**< Both at 952 Hz */
#define LSM9DS1_XL_OFF_GY_14Hz9_LP 0x81 /**< Accel off, gyro 14.9 Hz (low power) */
#define LSM9DS1_XL_OFF_GY_59Hz5_LP 0x82 /**< Accel off, gyro 59.5 Hz (low power) */
#define LSM9DS1_XL_OFF_GY_119Hz_LP 0x83 /**< Accel off, gyro 119 Hz (low power) */
#define LSM9DS1_IMU_14Hz9_LP      0x91 /**< Both at 14.9 Hz (gyro low power) */
#define LSM9DS1_IMU_59Hz5_LP      0xA2 /**< Both at 59.5 Hz (gyro low power) */
#define LSM9DS1_IMU_119Hz_LP      0xB3 /**< Both at 119 Hz (gyro low power) */
/** @} */

/**
 * @defgroup lsm9ds1_mag_range Magnetometer full-scale range
 * @{
 */
#define LSM9DS1_DT_FS_4Ga  0 /**< ±4 gauss */
#define LSM9DS1_DT_FS_8Ga  1 /**< ±8 gauss */
#define LSM9DS1_DT_FS_12Ga 2 /**< ±12 gauss */
#define LSM9DS1_DT_FS_16Ga 3 /**< ±16 gauss */
/** @} */

/**
 * @defgroup lsm9ds1_mag_odr Magnetometer output data rates
 *
 * The encoded value combines performance mode (bits 5:4) and ODR (bits 3:0).
 * @{
 */
#define LSM9DS1_MAG_POWER_DOWN 0xC0 /**< Power-down */
#define LSM9DS1_MAG_LP_0Hz625  0x00 /**< Low power, 0.625 Hz */
#define LSM9DS1_MAG_LP_1Hz25   0x01 /**< Low power, 1.25 Hz */
#define LSM9DS1_MAG_LP_2Hz5    0x02 /**< Low power, 2.5 Hz */
#define LSM9DS1_MAG_LP_5Hz     0x03 /**< Low power, 5 Hz */
#define LSM9DS1_MAG_LP_10Hz    0x04 /**< Low power, 10 Hz */
#define LSM9DS1_MAG_LP_20Hz    0x05 /**< Low power, 20 Hz */
#define LSM9DS1_MAG_LP_40Hz    0x06 /**< Low power, 40 Hz */
#define LSM9DS1_MAG_LP_80Hz    0x07 /**< Low power, 80 Hz */
#define LSM9DS1_MAG_LP_1000Hz  0x08 /**< Low power, 1000 Hz */
#define LSM9DS1_MAG_MP_0Hz625  0x10 /**< Medium performance, 0.625 Hz */
#define LSM9DS1_MAG_MP_1Hz25   0x11 /**< Medium performance, 1.25 Hz */
#define LSM9DS1_MAG_MP_2Hz5    0x12 /**< Medium performance, 2.5 Hz */
#define LSM9DS1_MAG_MP_5Hz     0x13 /**< Medium performance, 5 Hz */
#define LSM9DS1_MAG_MP_10Hz    0x14 /**< Medium performance, 10 Hz */
#define LSM9DS1_MAG_MP_20Hz    0x15 /**< Medium performance, 20 Hz */
#define LSM9DS1_MAG_MP_40Hz    0x16 /**< Medium performance, 40 Hz */
#define LSM9DS1_MAG_MP_80Hz    0x17 /**< Medium performance, 80 Hz */
#define LSM9DS1_MAG_MP_560Hz   0x18 /**< Medium performance, 560 Hz */
#define LSM9DS1_MAG_HP_0Hz625  0x20 /**< High performance, 0.625 Hz */
#define LSM9DS1_MAG_HP_1Hz25   0x21 /**< High performance, 1.25 Hz */
#define LSM9DS1_MAG_HP_2Hz5    0x22 /**< High performance, 2.5 Hz */
#define LSM9DS1_MAG_HP_5Hz     0x23 /**< High performance, 5 Hz */
#define LSM9DS1_MAG_HP_10Hz    0x24 /**< High performance, 10 Hz */
#define LSM9DS1_MAG_HP_20Hz    0x25 /**< High performance, 20 Hz */
#define LSM9DS1_MAG_HP_40Hz    0x26 /**< High performance, 40 Hz */
#define LSM9DS1_MAG_HP_80Hz    0x27 /**< High performance, 80 Hz */
#define LSM9DS1_MAG_HP_300Hz   0x28 /**< High performance, 300 Hz */
#define LSM9DS1_MAG_UHP_0Hz625 0x30 /**< Ultra-high performance, 0.625 Hz */
#define LSM9DS1_MAG_UHP_1Hz25  0x31 /**< Ultra-high performance, 1.25 Hz */
#define LSM9DS1_MAG_UHP_2Hz5   0x32 /**< Ultra-high performance, 2.5 Hz */
#define LSM9DS1_MAG_UHP_5Hz    0x33 /**< Ultra-high performance, 5 Hz */
#define LSM9DS1_MAG_UHP_10Hz   0x34 /**< Ultra-high performance, 10 Hz */
#define LSM9DS1_MAG_UHP_20Hz   0x35 /**< Ultra-high performance, 20 Hz */
#define LSM9DS1_MAG_UHP_40Hz   0x36 /**< Ultra-high performance, 40 Hz */
#define LSM9DS1_MAG_UHP_80Hz   0x37 /**< Ultra-high performance, 80 Hz */
#define LSM9DS1_MAG_UHP_155Hz  0x38 /**< Ultra-high performance, 155 Hz */
#define LSM9DS1_MAG_ONE_SHOT   0x70 /**< One-shot (single measurement) */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM9DS1_H_ */
