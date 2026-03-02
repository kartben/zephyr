/**
 * @file
 * @brief ST IIS3DWB high-bandwidth vibration sensor Devicetree constants
 */

/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_IIS3DWB_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_IIS3DWB_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup iis3dwb_dt_api ST IIS3DWB Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup iis3dwb_odr Output data rates
 * @{
 */
#define IIS3DWB_DT_ODR_OFF      0 /**< Power-down */
#define IIS3DWB_DT_ODR_26k7Hz   5 /**< 26.7 kHz */
/** @} */

/**
 * @defgroup iis3dwb_accel_range Accelerometer full-scale range
 * @{
 */
#define IIS3DWB_DT_FS_2G  0 /**< ±2 g  (0.061 mg/LSB) */
#define IIS3DWB_DT_FS_16G 1 /**< ±16 g (0.488 mg/LSB) */
#define IIS3DWB_DT_FS_4G  2 /**< ±4 g  (0.122 mg/LSB) */
#define IIS3DWB_DT_FS_8G  3 /**< ±8 g  (0.244 mg/LSB) */
/** @} */

/**
 * @defgroup iis3dwb_filter Filter settings
 * @{
 */
#define IIS3DWB_DT_SLOPE_ODR_DIV_4  0x10 /**< Slope filter, ODR/4 */
#define IIS3DWB_DT_HP_REF_MODE      0x37 /**< High-pass reference mode */
#define IIS3DWB_DT_HP_ODR_DIV_10    0x11 /**< High-pass, ODR/10 */
#define IIS3DWB_DT_HP_ODR_DIV_20    0x12 /**< High-pass, ODR/20 */
#define IIS3DWB_DT_HP_ODR_DIV_45    0x13 /**< High-pass, ODR/45 */
#define IIS3DWB_DT_HP_ODR_DIV_100   0x14 /**< High-pass, ODR/100 */
#define IIS3DWB_DT_HP_ODR_DIV_200   0x15 /**< High-pass, ODR/200 */
#define IIS3DWB_DT_HP_ODR_DIV_400   0x16 /**< High-pass, ODR/400 */
#define IIS3DWB_DT_HP_ODR_DIV_800   0x17 /**< High-pass, ODR/800 */
#define IIS3DWB_DT_LP_6k3Hz         0x00 /**< Low-pass, 6.3 kHz */
#define IIS3DWB_DT_LP_ODR_DIV_4     0x80 /**< Low-pass, ODR/4 */
#define IIS3DWB_DT_LP_ODR_DIV_10    0x81 /**< Low-pass, ODR/10 */
#define IIS3DWB_DT_LP_ODR_DIV_20    0x82 /**< Low-pass, ODR/20 */
#define IIS3DWB_DT_LP_ODR_DIV_45    0x83 /**< Low-pass, ODR/45 */
#define IIS3DWB_DT_LP_ODR_DIV_100   0x84 /**< Low-pass, ODR/100 */
#define IIS3DWB_DT_LP_ODR_DIV_200   0x85 /**< Low-pass, ODR/200 */
#define IIS3DWB_DT_LP_ODR_DIV_400   0x86 /**< Low-pass, ODR/400 */
#define IIS3DWB_DT_LP_ODR_DIV_800   0x87 /**< Low-pass, ODR/800 */
/** @} */

/**
 * @defgroup iis3dwb_xl_bdr Accelerometer FIFO batching data rate
 * @{
 */
#define IIS3DWB_DT_XL_NOT_BATCHED        0x0 /**< Not batched */
#define IIS3DWB_DT_XL_BATCHED_AT_26k7Hz  0xA /**< Batched at 26.7 kHz */
/** @} */

/**
 * @defgroup iis3dwb_temp_bdr Temperature FIFO batching data rate
 * @{
 */
#define IIS3DWB_DT_TEMP_NOT_BATCHED      0 /**< Not batched */
#define IIS3DWB_DT_TEMP_BATCHED_AT_104Hz 3 /**< Batched at 104 Hz */
/** @} */

/**
 * @defgroup iis3dwb_dec_ts Timestamp decimation in FIFO
 * @{
 */
#define IIS3DWB_DT_TS_NOT_BATCHED 0x0 /**< Timestamp not stored */
#define IIS3DWB_DT_DEC_TS_1       0x1 /**< Every sample */
#define IIS3DWB_DT_DEC_TS_8       0x2 /**< Every 8th sample */
#define IIS3DWB_DT_DEC_TS_32      0x3 /**< Every 32nd sample */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_IIS3DWB_H_ */
