/**
 * @file
 * @brief ST LSM6DSV-family common Devicetree constants
 *
 * Shared output data rates, batch rates, Sensor Fusion Low Power (SFLP)
 * options, and FIFO tag values used by LSM6DSV16X, LSM6DSV32X, LSM6DSV80X
 * and LSM6DSV320X chip-specific headers.
 */

/*
 * Copyright (c) 2025 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSVXXX_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSVXXX_H_

/**
 * @defgroup lsm6dsvxxx_dt_api ST LSM6DSV-family common Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lsm6dsvxxx_odr Accelerometer and gyroscope output data rates
 * @{
 */
#define LSM6DSVXXX_DT_ODR_OFF			0x0  /**< Power-down */
#define LSM6DSVXXX_DT_ODR_AT_1Hz875		0x1  /**< 1.875 Hz */
#define LSM6DSVXXX_DT_ODR_AT_7Hz5		0x2  /**< 7.5 Hz */
#define LSM6DSVXXX_DT_ODR_AT_15Hz		0x3  /**< 15 Hz */
#define LSM6DSVXXX_DT_ODR_AT_30Hz		0x4  /**< 30 Hz */
#define LSM6DSVXXX_DT_ODR_AT_60Hz		0x5  /**< 60 Hz */
#define LSM6DSVXXX_DT_ODR_AT_120Hz		0x6  /**< 120 Hz */
#define LSM6DSVXXX_DT_ODR_AT_240Hz		0x7  /**< 240 Hz */
#define LSM6DSVXXX_DT_ODR_AT_480Hz		0x8  /**< 480 Hz */
#define LSM6DSVXXX_DT_ODR_AT_960Hz		0x9  /**< 960 Hz */
#define LSM6DSVXXX_DT_ODR_AT_1920Hz		0xA  /**< 1920 Hz */
#define LSM6DSVXXX_DT_ODR_AT_3840Hz		0xB  /**< 3840 Hz */
#define LSM6DSVXXX_DT_ODR_AT_7680Hz		0xC  /**< 7680 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_15Hz625	0x13 /**< HA01 mode: 15.625 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_31Hz25	0x14 /**< HA01 mode: 31.25 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_62Hz5		0x15 /**< HA01 mode: 62.5 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_125Hz		0x16 /**< HA01 mode: 125 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_250Hz		0x17 /**< HA01 mode: 250 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_500Hz		0x18 /**< HA01 mode: 500 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_1000Hz	0x19 /**< HA01 mode: 1000 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_2000Hz	0x1A /**< HA01 mode: 2000 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_4000Hz	0x1B /**< HA01 mode: 4000 Hz */
#define LSM6DSVXXX_DT_ODR_HA01_AT_8000Hz	0x1C /**< HA01 mode: 8000 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_12Hz5		0x23 /**< HA02 mode: 12.5 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_25Hz		0x24 /**< HA02 mode: 25 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_50Hz		0x25 /**< HA02 mode: 50 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_100Hz		0x26 /**< HA02 mode: 100 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_200Hz		0x27 /**< HA02 mode: 200 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_400Hz		0x28 /**< HA02 mode: 400 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_800Hz		0x29 /**< HA02 mode: 800 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_1600Hz	0x2A /**< HA02 mode: 1600 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_3200Hz	0x2B /**< HA02 mode: 3200 Hz */
#define LSM6DSVXXX_DT_ODR_HA02_AT_6400Hz	0x2C /**< HA02 mode: 6400 Hz */
/** @} */

/**
 * @defgroup lsm6dsvxxx_xl_batch Accelerometer FIFO batching data rates
 * @{
 */
#define LSM6DSVXXX_DT_XL_NOT_BATCHED		0x0 /**< Not batched */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_1Hz875	0x1 /**< 1.875 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_7Hz5	0x2 /**< 7.5 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_15Hz	0x3 /**< 15 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_30Hz	0x4 /**< 30 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_60Hz	0x5 /**< 60 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_120Hz	0x6 /**< 120 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_240Hz	0x7 /**< 240 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_480Hz	0x8 /**< 480 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_960Hz	0x9 /**< 960 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_1920Hz	0xa /**< 1920 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_3840Hz	0xb /**< 3840 Hz */
#define LSM6DSVXXX_DT_XL_BATCHED_AT_7680Hz	0xc /**< 7680 Hz */
/** @} */

/**
 * @defgroup lsm6dsvxxx_gy_batch Gyroscope FIFO batching data rates
 * @{
 */
#define LSM6DSVXXX_DT_GY_NOT_BATCHED		0x0 /**< Not batched */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_1Hz875	0x1 /**< 1.875 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_7Hz5	0x2 /**< 7.5 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_15Hz	0x3 /**< 15 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_30Hz	0x4 /**< 30 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_60Hz	0x5 /**< 60 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_120Hz	0x6 /**< 120 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_240Hz	0x7 /**< 240 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_480Hz	0x8 /**< 480 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_960Hz	0x9 /**< 960 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_1920Hz	0xa /**< 1920 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_3840Hz	0xb /**< 3840 Hz */
#define LSM6DSVXXX_DT_GY_BATCHED_AT_7680Hz	0xc /**< 7680 Hz */
/** @} */

/**
 * @defgroup lsm6dsvxxx_temp_batch Temperature sensor FIFO batching data rates
 * @{
 */
#define LSM6DSVXXX_DT_TEMP_NOT_BATCHED		0x0 /**< Not batched */
#define LSM6DSVXXX_DT_TEMP_BATCHED_AT_1Hz875	0x1 /**< 1.875 Hz */
#define LSM6DSVXXX_DT_TEMP_BATCHED_AT_15Hz	0x2 /**< 15 Hz */
#define LSM6DSVXXX_DT_TEMP_BATCHED_AT_60Hz	0x3 /**< 60 Hz */
/** @} */

/**
 * @defgroup lsm6dsvxxx_sflp_odr Sensor Fusion Low Power (SFLP) output data rates
 * @{
 */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_15Hz		0x0 /**< 15 Hz */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_30Hz		0x1 /**< 30 Hz */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_60Hz		0x2 /**< 60 Hz */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_120Hz		0x3 /**< 120 Hz */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_240Hz		0x4 /**< 240 Hz */
#define LSM6DSVXXX_DT_SFLP_ODR_AT_480Hz		0x5 /**< 480 Hz */
/** @} */

/**
 * @defgroup lsm6dsvxxx_sflp_fifo Sensor Fusion Low Power (SFLP) FIFO enable options
 *
 * Bitmask values to select which SFLP vectors are stored in the FIFO.
 * @{
 */
#define LSM6DSVXXX_DT_SFLP_FIFO_OFF					0x0 /**< SFLP FIFO disabled */
#define LSM6DSVXXX_DT_SFLP_FIFO_GAME_ROTATION				0x1 /**< Game rotation vector */
#define LSM6DSVXXX_DT_SFLP_FIFO_GRAVITY				0x2 /**< Gravity vector */
#define LSM6DSVXXX_DT_SFLP_FIFO_GAME_ROTATION_GRAVITY			0x3 /**< Game rotation + gravity */
#define LSM6DSVXXX_DT_SFLP_FIFO_GBIAS					0x4 /**< Gyroscope bias */
#define LSM6DSVXXX_DT_SFLP_FIFO_GAME_ROTATION_GBIAS			0x5 /**< Game rotation + gyroscope bias */
#define LSM6DSVXXX_DT_SFLP_FIFO_GRAVITY_GBIAS				0x6 /**< Gravity + gyroscope bias */
#define LSM6DSVXXX_DT_SFLP_FIFO_GAME_ROTATION_GRAVITY_GBIAS		0x7 /**< Game rotation + gravity + gyroscope bias */
/** @} */

/**
 * @defgroup lsm6dsvxxx_fifo_tags FIFO tag identifiers
 *
 * Tag values stored in the FIFO data-out tag byte, identifying the sensor
 * that produced each FIFO word.
 * @{
 */
#define LSM6DSVXXX_FIFO_EMPTY                    0x0  /**< FIFO empty */
#define LSM6DSVXXX_GY_NC_TAG                     0x1  /**< Gyroscope (not compressed) */
#define LSM6DSVXXX_XL_NC_TAG                     0x2  /**< Accelerometer (not compressed) */
#define LSM6DSVXXX_TEMPERATURE_TAG               0x3  /**< Temperature sensor */
#define LSM6DSVXXX_TIMESTAMP_TAG                 0x4  /**< Timestamp */
#define LSM6DSVXXX_CFG_CHANGE_TAG                0x5  /**< Configuration change */
#define LSM6DSVXXX_XL_NC_T_2_TAG                 0x6  /**< Accelerometer (not compressed, T-2) */
#define LSM6DSVXXX_XL_NC_T_1_TAG                 0x7  /**< Accelerometer (not compressed, T-1) */
#define LSM6DSVXXX_XL_2XC_TAG                    0x8  /**< Accelerometer (2x compressed) */
#define LSM6DSVXXX_XL_3XC_TAG                    0x9  /**< Accelerometer (3x compressed) */
#define LSM6DSVXXX_GY_NC_T_2_TAG                 0xA  /**< Gyroscope (not compressed, T-2) */
#define LSM6DSVXXX_GY_NC_T_1_TAG                 0xB  /**< Gyroscope (not compressed, T-1) */
#define LSM6DSVXXX_GY_2XC_TAG                    0xC  /**< Gyroscope (2x compressed) */
#define LSM6DSVXXX_GY_3XC_TAG                    0xD  /**< Gyroscope (3x compressed) */
#define LSM6DSVXXX_SENSORHUB_TARGET0_TAG         0xE  /**< Sensor hub target 0 */
#define LSM6DSVXXX_SENSORHUB_TARGET1_TAG         0xF  /**< Sensor hub target 1 */
#define LSM6DSVXXX_SENSORHUB_TARGET2_TAG         0x10 /**< Sensor hub target 2 */
#define LSM6DSVXXX_SENSORHUB_TARGET3_TAG         0x11 /**< Sensor hub target 3 */
#define LSM6DSVXXX_STEP_COUNTER_TAG              0x12 /**< Step counter */
#define LSM6DSVXXX_SFLP_GAME_ROTATION_VECTOR_TAG 0x13 /**< SFLP game rotation vector */
#define LSM6DSVXXX_SFLP_GYROSCOPE_BIAS_TAG       0x16 /**< SFLP gyroscope bias */
#define LSM6DSVXXX_SFLP_GRAVITY_VECTOR_TAG       0x17 /**< SFLP gravity vector */
#define LSM6DSVXXX_HG_XL_PEAK_TAG                0x18 /**< High-g accelerometer peak */
#define LSM6DSVXXX_SENSORHUB_NACK_TAG            0x19 /**< Sensor hub NACK */
#define LSM6DSVXXX_MLC_RESULT_TAG                0x1A /**< Machine learning core result */
#define LSM6DSVXXX_MLC_FILTER                    0x1B /**< Machine learning core filter */
#define LSM6DSVXXX_MLC_FEATURE                   0x1C /**< Machine learning core feature */
#define LSM6DSVXXX_XL_HG_TAG                     0x1D /**< High-g accelerometer */
#define LSM6DSVXXX_GY_ENHANCED_EIS               0x1E /**< Gyroscope enhanced EIS */
/** @} */

/** @} */ /* lsm6dsvxxx_dt_api */

/** @cond INTERNAL */
/* Internal register addresses — not intended for Devicetree use */
#define LSM6DSVXXX_STATUS_REG			0x1EU
#define LSM6DSVXXX_OUTX_L_A			0x28U
#define LSM6DSVXXX_FIFO_STATUS1			0x1BU
#define LSM6DSVXXX_FIFO_DATA_OUT_TAG		0x78U
#define LSM6DSVXXX_BYPASS_MODE			0x00U
#define LSM6DSVXXX_FIFO_CTRL4			0x0AU
/** @endcond */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LSM6DSVXXX_H_ */
