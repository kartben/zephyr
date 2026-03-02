/**
 * @file
 * @brief TI TMAG5273 Hall-effect magnetic sensor Devicetree constants
 */

/*
 * Copyright (c) 2023 deveritec GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_TMAG5273_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_TMAG5273_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup tmag5273_dt_api TI TMAG5273 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup tmag5273_oper_mode Operating mode
 * @{
 */
#define TMAG5273_DT_OPER_MODE_CONTINUOUS 0 /**< Continuous measurement */
#define TMAG5273_DT_OPER_MODE_STANDBY    1 /**< Standby */
/** @} */

/**
 * @defgroup tmag5273_axis Axis enable bitmask
 * @{
 */
#define TMAG5273_DT_AXIS_NONE 0x0                                             /**< No axis */
#define TMAG5273_DT_AXIS_X    0x1                                             /**< X axis */
#define TMAG5273_DT_AXIS_Y    0x2                                             /**< Y axis */
#define TMAG5273_DT_AXIS_Z    0x4                                             /**< Z axis */
#define TMAG5273_DT_AXIS_XY   (TMAG5273_DT_AXIS_X | TMAG5273_DT_AXIS_Y)     /**< X + Y axes */
#define TMAG5273_DT_AXIS_XZ   (TMAG5273_DT_AXIS_X | TMAG5273_DT_AXIS_Z)     /**< X + Z axes */
#define TMAG5273_DT_AXIS_YZ   (TMAG5273_DT_AXIS_Y | TMAG5273_DT_AXIS_Z)     /**< Y + Z axes */
#define TMAG5273_DT_AXIS_XYZ  (TMAG5273_DT_AXIS_X | TMAG5273_DT_AXIS_Y | TMAG5273_DT_AXIS_Z) /**< All axes */
#define TMAG5273_DT_AXIS_XYX  0x8 /**< X-Y-X sequence */
#define TMAG5273_DT_AXIS_YXY  0x9 /**< Y-X-Y sequence */
#define TMAG5273_DT_AXIS_YZY  0xA /**< Y-Z-Y sequence */
#define TMAG5273_DT_AXIS_XZX  0xB /**< X-Z-X sequence */
/** @} */

/**
 * @defgroup tmag5273_range Magnetic field full-scale range
 * @{
 */
#define TMAG5273_DT_AXIS_RANGE_LOW     0 /**< Low range */
#define TMAG5273_DT_AXIS_RANGE_HIGH    1 /**< High range */
#define TMAG5273_DT_AXIS_RANGE_RUNTIME 2 /**< Configured at runtime */
/** @} */

/**
 * @defgroup tmag5273_int_mode Interrupt pin routing mode
 * @{
 */
#define TMAG5273_DT_INT_THROUGH_INT         0 /**< Via INT pin */
#define TMAG5273_DT_INT_THROUGH_INT_EXC_I2C 1 /**< Via INT pin, except during I2C */
#define TMAG5273_DT_INT_THROUGH_SCL         2 /**< Via SCL pin */
#define TMAG5273_DT_INT_THROUGH_SCL_EXC_I2C 3 /**< Via SCL pin, except during I2C */
/** @} */

/**
 * @defgroup tmag5273_thrx_count Threshold crossing count
 * @{
 */
#define TMAG5273_DT_THRX_COUNT_1 0 /**< 1 crossing */
#define TMAG5273_DT_THRX_COUNT_4 1 /**< 4 crossings */
/** @} */

/**
 * @defgroup tmag5273_thrx_dir Threshold crossing direction
 * @{
 */
#define TMAG5273_DT_THRX_ABOVE   0 /**< Above threshold */
#define TMAG5273_DT_THRX_BELOW   1 /**< Below threshold */
#define TMAG5273_DT_THRX_OUTSIDE 2 /**< Outside window */
#define TMAG5273_DT_THRX_INSIDE  3 /**< Inside window */
/** @} */

/**
 * @defgroup tmag5273_temp_coeff Temperature coefficient for magnet type
 * @{
 */
#define TMAG5273_DT_TEMP_COEFF_NONE    0 /**< None */
#define TMAG5273_DT_TEMP_COEFF_NDBFE   1 /**< NdBFe magnet */
#define TMAG5273_DT_TEMP_COEFF_CERAMIC 2 /**< Ceramic ferrite magnet */
/** @} */

/**
 * @defgroup tmag5273_angle_mag Angle/magnitude calculation axis pair
 * @{
 */
#define TMAG5273_DT_ANGLE_MAG_NONE    0 /**< Disabled */
#define TMAG5273_DT_ANGLE_MAG_XY      1 /**< X-Y plane */
#define TMAG5273_DT_ANGLE_MAG_YZ      2 /**< Y-Z plane */
#define TMAG5273_DT_ANGLE_MAG_XZ      3 /**< X-Z plane */
#define TMAG5273_DT_ANGLE_MAG_RUNTIME 4 /**< Configured at runtime */
/** @} */

/**
 * @defgroup tmag5273_gain_corr Channel magnitude gain correction channel
 * @{
 */
#define TMAG5273_DT_CORRECTION_CH_1 0 /**< Channel 1 */
#define TMAG5273_DT_CORRECTION_CH_2 1 /**< Channel 2 */
/** @} */

/**
 * @defgroup tmag5273_averaging Averaging count
 * @{
 */
#define TMAG5273_DT_AVERAGING_NONE 0 /**< No averaging */
#define TMAG5273_DT_AVERAGING_2X   1 /**<  2x averaging */
#define TMAG5273_DT_AVERAGING_4X   2 /**<  4x averaging */
#define TMAG5273_DT_AVERAGING_8X   3 /**<  8x averaging */
#define TMAG5273_DT_AVERAGING_16X  4 /**< 16x averaging */
#define TMAG5273_DT_AVERAGING_32X  5 /**< 32x averaging */
/** @} */

/**
 * @defgroup tmag5273_sleeptime Standby sleep time
 * @{
 */
#define TMAG5273_DT_SLEEPTIME_1MS     0  /**< 1 ms */
#define TMAG5273_DT_SLEEPTIME_5MS     1  /**< 5 ms */
#define TMAG5273_DT_SLEEPTIME_10MS    2  /**< 10 ms */
#define TMAG5273_DT_SLEEPTIME_15MS    3  /**< 15 ms */
#define TMAG5273_DT_SLEEPTIME_20MS    4  /**< 20 ms */
#define TMAG5273_DT_SLEEPTIME_30MS    5  /**< 30 ms */
#define TMAG5273_DT_SLEEPTIME_50MS    6  /**< 50 ms */
#define TMAG5273_DT_SLEEPTIME_100MS   7  /**< 100 ms */
#define TMAG5273_DT_SLEEPTIME_500MS   8  /**< 500 ms */
#define TMAG5273_DT_SLEEPTIME_1000MS  9  /**< 1000 ms */
#define TMAG5273_DT_SLEEPTIME_2000MS  10 /**< 2000 ms */
#define TMAG5273_DT_SLEEPTIME_5000MS  11 /**< 5000 ms */
#define TMAG5273_DT_SLEEPTIME_20000MS 12 /**< 20000 ms */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_TMAG5273_H_ */
