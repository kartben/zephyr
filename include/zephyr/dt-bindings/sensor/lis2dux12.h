/**
 * @file
 * @brief ST LIS2DUX12 accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_LIS2DUX12_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_LIS2DUX12_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup lis2dux12_dt_api ST LIS2DUX12 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lis2dux12_oper_mode Operating modes
 * @{
 */
#define LIS2DUX12_OPER_MODE_POWER_DOWN        0 /**< Power-down */
#define LIS2DUX12_OPER_MODE_LOW_POWER         1 /**< Low power mode */
#define LIS2DUX12_OPER_MODE_HIGH_PERFORMANCE  2 /**< High performance mode */
#define LIS2DUX12_OPER_MODE_SINGLE_SHOT       3 /**< Single-shot mode */
/** @} */

/**
 * @defgroup lis2dux12_odr Output data rates
 * @{
 */
#define LIS2DUX12_DT_ODR_OFF      0  /**< Power-down */
#define LIS2DUX12_DT_ODR_1Hz_ULP 1  /**< 1 Hz (ultra-low power mode only) */
#define LIS2DUX12_DT_ODR_3Hz_ULP 2  /**< 3 Hz (ultra-low power mode only) */
#define LIS2DUX12_DT_ODR_25Hz_ULP 3 /**< 25 Hz (ultra-low power mode only) */
#define LIS2DUX12_DT_ODR_6Hz      4 /**< 6 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_12Hz5    5 /**< 12.5 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_25Hz     6 /**< 25 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_50Hz     7 /**< 50 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_100Hz    8 /**< 100 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_200Hz    9 /**< 200 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_400Hz    10 /**< 400 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_800Hz    11 /**< 800 Hz (LP and HP modes) */
#define LIS2DUX12_DT_ODR_END      12 /**< Sentinel value */
/** @} */

/**
 * @defgroup lis2dux12_accel_range Accelerometer full-scale range
 * @{
 */
#define LIS2DUX12_DT_FS_2G  0 /**< ±2 g  (0.061 mg/LSB) */
#define LIS2DUX12_DT_FS_4G  1 /**< ±4 g  (0.122 mg/LSB) */
#define LIS2DUX12_DT_FS_8G  2 /**< ±8 g  (0.244 mg/LSB) */
#define LIS2DUX12_DT_FS_16G 3 /**< ±16 g (0.488 mg/LSB) */
/** @} */

/**
 * @defgroup lis2dux12_bdr_xl Accelerometer FIFO batching data rate
 * @{
 */
#define LIS2DUX12_DT_BDR_XL_ODR        0x0 /**< Same as ODR */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_2  0x1 /**< ODR/2 */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_4  0x2 /**< ODR/4 */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_8  0x3 /**< ODR/8 */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_16 0x4 /**< ODR/16 */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_32 0x5 /**< ODR/32 */
#define LIS2DUX12_DT_BDR_XL_ODR_DIV_64 0x6 /**< ODR/64 */
#define LIS2DUX12_DT_BDR_XL_ODR_OFF    0x7 /**< Not batched */
/** @} */

/**
 * @defgroup lis2dux12_dec_ts Timestamp decimation in FIFO
 * @{
 */
#define LIS2DUX12_DT_DEC_TS_OFF  0x0 /**< Timestamp not stored */
#define LIS2DUX12_DT_DEC_TS_1    0x1 /**< Every sample */
#define LIS2DUX12_DT_DEC_TS_8    0x2 /**< Every 8th sample */
#define LIS2DUX12_DT_DEC_TS_32   0x3 /**< Every 32nd sample */
/** @} */

/**
 * @defgroup lis2dux12_fifo_tags FIFO tag identifiers
 * @{
 */
#define LIS2DUXXX_FIFO_EMPTY         0x0  /**< FIFO empty */
#define LIS2DUXXX_XL_TEMP_TAG        0x2  /**< Accelerometer + temperature */
#define LIS2DUXXX_XL_ONLY_2X_TAG     0x3  /**< Accelerometer (2x compressed) */
#define LIS2DUXXX_TIMESTAMP_TAG      0x4  /**< Timestamp */
#define LIS2DUXXX_STEP_COUNTER_TAG   0x12 /**< Step counter */
#define LIS2DUXXX_MLC_RESULT_TAG     0x1A /**< Machine learning core result */
#define LIS2DUXXX_MLC_FILTER_TAG     0x1B /**< Machine learning core filter */
#define LIS2DUXXX_MLC_FEATURE        0x1C /**< Machine learning core feature */
#define LIS2DUXXX_FSM_RESULT_TAG     0x1D /**< Finite state machine result */
/** @} */

/**
 * @defgroup lis2dux12_fifo_modes FIFO operating modes
 * @{
 */
#define LIS2DUXXX_DT_BYPASS_MODE            0x0 /**< Bypass (FIFO disabled) */
#define LIS2DUXXX_DT_FIFO_MODE              0x1 /**< FIFO mode: stops collecting on full */
#define LIS2DUXXX_DT_STREAM_TO_FIFO_MODE    0x3 /**< Stream-to-FIFO: switch at trigger */
#define LIS2DUXXX_DT_BYPASS_TO_STREAM_MODE  0x4 /**< Bypass-to-stream: switch at trigger */
#define LIS2DUXXX_DT_STREAM_MODE            0x6 /**< Continuous stream (overwrite oldest) */
#define LIS2DUXXX_DT_BYPASS_TO_FIFO_MODE    0x7 /**< Bypass-to-FIFO: switch at trigger */
#define LIS2DUXXX_DT_FIFO_OFF               0x8 /**< FIFO off */
/** @} */

/** @} */ /* lis2dux12_dt_api */

/** @cond INTERNAL */
/* Internal register addresses — not intended for Devicetree use */
#define LIS2DUXXX_DT_FIFO_CTRL           0x15U
#define LIS2DUXXX_DT_STATUS              0x25U
#define LIS2DUXXX_DT_FIFO_STATUS1        0x26U
#define LIS2DUXXX_DT_OUTX_L              0x28U
#define LIS2DUXXX_DT_FIFO_DATA_OUT_TAG   0x40U
/** @endcond */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_LIS2DUX12_H_ */
