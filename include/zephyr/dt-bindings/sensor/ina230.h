/**
 * @file
 * @brief TI INA230 power monitor Devicetree constants
 */

/*
 * Copyright 2021 The Chromium OS Authors
 * Copyright (c) 2021 Grinn
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INA230_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INA230_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup ina230_dt_api TI INA230 power monitor Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup ina230_alert_mask Alert pin Mask/Enable bits
 * @{
 */
#define INA230_SHUNT_VOLTAGE_OVER    BIT(15) /**< Shunt voltage over-limit */
#define INA230_SHUNT_VOLTAGE_UNDER   BIT(14) /**< Shunt voltage under-limit */
#define INA230_BUS_VOLTAGE_OVER      BIT(13) /**< Bus voltage over-limit */
#define INA230_BUS_VOLTAGE_UNDER     BIT(12) /**< Bus voltage under-limit */
#define INA230_OVER_LIMIT_POWER      BIT(11) /**< Power over-limit */
#define INA230_CONVERSION_READY      BIT(10) /**< Conversion ready */
#define INA230_ALERT_FUNCTION_FLAG   BIT(4)  /**< Alert function flag */
#define INA230_CONVERSION_READY_FLAG BIT(3)  /**< Conversion ready flag */
#define INA230_MATH_OVERFLOW_FLAG    BIT(2)  /**< Math overflow flag */
#define INA230_ALERT_POLARITY        BIT(1)  /**< Alert polarity (1=active high) */
#define INA230_ALERT_LATCH_ENABLE    BIT(0)  /**< Alert latch enable */
/** @} */

/**
 * @defgroup ina230_oper_mode Operating mode
 * @{
 */
#define INA230_OPER_MODE_POWER_DOWN             0x00 /**< Power-down */
#define INA230_OPER_MODE_SHUNT_VOLTAGE_TRIG     0x01 /**< Shunt voltage, triggered */
#define INA230_OPER_MODE_BUS_VOLTAGE_TRIG       0x02 /**< Bus voltage, triggered */
#define INA230_OPER_MODE_SHUNT_BUS_VOLTAGE_TRIG 0x03 /**< Shunt + bus voltage, triggered */
#define INA230_OPER_MODE_SHUNT_VOLTAGE_CONT     0x05 /**< Shunt voltage, continuous */
#define INA230_OPER_MODE_BUS_VOLTAGE_CONT       0x06 /**< Bus voltage, continuous */
#define INA230_OPER_MODE_SHUNT_BUS_VOLTAGE_CONT 0x07 /**< Shunt + bus voltage, continuous */
/** @} */

/**
 * @defgroup ina230_conv_time Conversion time for bus and shunt (µs)
 * @{
 */
#define INA230_CONV_TIME_140  0x00 /**< 140 µs */
#define INA230_CONV_TIME_204  0x01 /**< 204 µs */
#define INA230_CONV_TIME_332  0x02 /**< 332 µs */
#define INA230_CONV_TIME_588  0x03 /**< 588 µs */
#define INA230_CONV_TIME_1100 0x04 /**< 1100 µs */
#define INA230_CONV_TIME_2116 0x05 /**< 2116 µs */
#define INA230_CONV_TIME_4156 0x06 /**< 4156 µs */
#define INA230_CONV_TIME_8244 0x07 /**< 8244 µs */
/** @} */

/**
 * @defgroup ina230_avg Averaging mode (number of samples)
 * @{
 */
#define INA230_AVG_MODE_1    0x00 /**< 1 sample (no averaging) */
#define INA230_AVG_MODE_4    0x01 /**< 4 samples */
#define INA230_AVG_MODE_16   0x02 /**< 16 samples */
#define INA230_AVG_MODE_64   0x03 /**< 64 samples */
#define INA230_AVG_MODE_128  0x04 /**< 128 samples */
#define INA230_AVG_MODE_256  0x05 /**< 256 samples */
#define INA230_AVG_MODE_512  0x06 /**< 512 samples */
#define INA230_AVG_MODE_1024 0x07 /**< 1024 samples */
/** @} */

/**
 * @brief Macro for creating the INA230 configuration register value
 *
 * @param mode  Operating mode.
 * @param svct  Conversion time for shunt voltage.
 * @param bvct  Conversion time for bus voltage.
 * @param avg   Averaging mode.
 */
#define INA230_CONFIG(mode, \
		      svct, \
		      bvct, \
		      avg)  \
	(((avg) << 9) | ((bvct) << 6) | ((svct) << 3) | (mode))

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INA230_H_ */
