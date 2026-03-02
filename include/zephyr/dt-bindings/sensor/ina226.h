/**
 * @file
 * @brief NXP INA226 power monitor Devicetree constants
 */

/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INA226_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INA226_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup ina226_dt_api INA226 power monitor Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup ina226_reset Reset mode
 * @{
 */
#define INA226_RST_NORMAL_OPERATION	0x00 /**< Normal operation */
#define INA226_RST_SYSTEM_RESET		0x01 /**< System reset */
/** @} */

/**
 * @defgroup ina226_avg Averaging mode (number of samples)
 * @{
 */
#define INA226_AVG_MODE_1		0x00 /**< 1 sample (no averaging) */
#define INA226_AVG_MODE_4		0x01 /**< 4 samples */
#define INA226_AVG_MODE_16		0x02 /**< 16 samples */
#define INA226_AVG_MODE_64		0x03 /**< 64 samples */
#define INA226_AVG_MODE_128		0x04 /**< 128 samples */
#define INA226_AVG_MODE_256		0x05 /**< 256 samples */
#define INA226_AVG_MODE_512		0x06 /**< 512 samples */
#define INA226_AVG_MODE_1024		0x07 /**< 1024 samples */
/** @} */

/**
 * @defgroup ina226_conv_time Conversion time for bus and shunt voltage (µs)
 * @{
 */
#define INA226_CONV_TIME_140		0x00 /**< 140 µs */
#define INA226_CONV_TIME_204		0x01 /**< 204 µs */
#define INA226_CONV_TIME_332		0x02 /**< 332 µs */
#define INA226_CONV_TIME_588		0x03 /**< 588 µs */
#define INA226_CONV_TIME_1100		0x04 /**< 1100 µs */
#define INA226_CONV_TIME_2116		0x05 /**< 2116 µs */
#define INA226_CONV_TIME_4156		0x06 /**< 4156 µs */
#define INA226_CONV_TIME_8244		0x07 /**< 8244 µs */
/** @} */

/**
 * @defgroup ina226_oper_mode Operating mode
 * @{
 */
#define INA226_OPER_MODE_POWER_DOWN			0x00 /**< Power-down */
#define INA226_OPER_MODE_SHUNT_VOLTAGE_TRIG		0x01 /**< Shunt voltage, triggered */
#define INA226_OPER_MODE_BUS_VOLTAGE_TRIG		0x02 /**< Bus voltage, triggered */
#define INA226_OPER_MODE_SHUNT_BUS_VOLTAGE_TRIG	0x03 /**< Shunt + bus voltage, triggered */
#define INA226_OPER_MODE_SHUNT_VOLTAGE_CONT		0x05 /**< Shunt voltage, continuous */
#define INA226_OPER_MODE_BUS_VOLTAGE_CONT		0x06 /**< Bus voltage, continuous */
#define INA226_OPER_MODE_SHUNT_BUS_VOLTAGE_CONT	0x07 /**< Shunt + bus voltage, continuous */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INA226_H_ */
