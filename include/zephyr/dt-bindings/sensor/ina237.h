/**
 * @file
 * @brief TI INA237 power monitor Devicetree constants
 */

/*
 * Copyright (c) 2021 Grinn
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_INA237_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_INA237_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @defgroup ina237_dt_api TI INA237 power monitor Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup ina237_oper_mode Operating mode
 * @{
 */
#define INA237_CFG_HIGH_PRECISION			BIT(4)  /**< Enable high precision mode */
#define INA237_OPER_MODE_SHUTDOWN			0x00 /**< Shutdown */
#define INA237_OPER_MODE_BUS_VOLTAGE_TRIG		0x01 /**< Bus voltage, triggered */
#define INA237_OPER_MODE_SHUNT_VOLTAGE_TRIG		0x02 /**< Shunt voltage, triggered */
#define INA237_OPER_MODE_SHUNT_BUS_VOLTAGE_TRIG	0x03 /**< Shunt + bus voltage, triggered */
#define INA237_OPER_MODE_TEMP_TRIG			0x04 /**< Temperature, triggered */
#define INA237_OPER_MODE_TEMP_BUS_VOLTAGE_TRIG		0x05 /**< Temperature + bus voltage, triggered */
#define INA237_OPER_MODE_TEMP_SHUNT_VOLTAGE_TRIG	0x06 /**< Temperature + shunt voltage, triggered */
#define INA237_OPER_MODE_BUS_SHUNT_VOLTAGE_TEMP_TRIG	0x07 /**< Bus + shunt + temperature, triggered */
#define INA237_OPER_MODE_BUS_VOLTAGE_CONT		0x09 /**< Bus voltage, continuous */
#define INA237_OPER_MODE_SHUNT_VOLTAGE_CONT		0x0A /**< Shunt voltage, continuous */
#define INA237_OPER_MODE_SHUNT_BUS_VOLTAGE_CONT	0x0B /**< Shunt + bus voltage, continuous */
#define INA237_OPER_MODE_TEMP_CONT			0x0C /**< Temperature, continuous */
#define INA237_OPER_MODE_BUS_VOLTAGE_TEMP_CONT		0x0D /**< Bus voltage + temperature, continuous */
#define INA237_OPER_MODE_TEMP_SHUNT_VOLTAGE_CONT	0x0E /**< Temperature + shunt voltage, continuous */
#define INA237_OPER_MODE_BUS_SHUNT_VOLTAGE_TEMP_CONT	0x0F /**< Bus + shunt + temperature, continuous */
/** @} */

/**
 * @defgroup ina237_conv_time Conversion time for bus, shunt, and temperature (µs)
 * @{
 */
#define INA237_CONV_TIME_50   0x00 /**<   50 µs */
#define INA237_CONV_TIME_84   0x01 /**<   84 µs */
#define INA237_CONV_TIME_150  0x02 /**<  150 µs */
#define INA237_CONV_TIME_280  0x03 /**<  280 µs */
#define INA237_CONV_TIME_540  0x04 /**<  540 µs */
#define INA237_CONV_TIME_1052 0x05 /**< 1052 µs */
#define INA237_CONV_TIME_2074 0x06 /**< 2074 µs */
#define INA237_CONV_TIME_4120 0x07 /**< 4120 µs */
/** @} */

/**
 * @defgroup ina237_avg Averaging mode (number of samples)
 * @{
 */
#define INA237_AVG_MODE_1    0x00 /**<    1 sample (no averaging) */
#define INA237_AVG_MODE_4    0x01 /**<    4 samples */
#define INA237_AVG_MODE_16   0x02 /**<   16 samples */
#define INA237_AVG_MODE_64   0x03 /**<   64 samples */
#define INA237_AVG_MODE_128  0x04 /**<  128 samples */
#define INA237_AVG_MODE_256  0x05 /**<  256 samples */
#define INA237_AVG_MODE_512  0x06 /**<  512 samples */
#define INA237_AVG_MODE_1024 0x07 /**< 1024 samples */
/** @} */

/**
 * @defgroup ina237_reset Reset mode
 * @{
 */
#define INA237_RST_NORMAL_OPERATION	0x00 /**< Normal operation */
#define INA237_RST_SYSTEM_RESET		0x01 /**< System reset */
/** @} */

/**
 * @defgroup ina237_init_delay Initial ADC conversion delay (steps of 2 ms)
 * @{
 */
#define INA237_INIT_ADC_DELAY_0_S	0x00 /**< No delay */
#define INA237_INIT_ADC_DELAY_2_MS	0x01 /**< 2 ms */
#define INA237_INIT_ADC_DELAY_510_MS	0xFF /**< 510 ms */
/** @} */

/**
 * @defgroup ina237_adc_range Shunt full-scale range
 * @{
 */
#define INA237_ADC_RANGE_163_84	0x00 /**< ±163.84 mV */
#define INA237_ADC_RANGE_40_96	0x01 /**< ±40.96 mV */
/** @} */

/**
 * @brief Macro for creating the INA237 configuration register value
 *
 * @param rst_mode  Reset mode.
 * @param convdly   Delay for initial ADC conversion in steps of 2 ms.
 * @param adc_range Shunt full-scale range.
 */
#define INA237_CONFIG(rst_mode, \
		      convdly, \
		      adc_range) \
	(((rst_mode) << 15) | ((convdly) << 6) | ((adc_range) << 4))

/**
 * @brief Macro for creating the INA237 ADC configuration register value
 *
 * @param mode   Operating mode.
 * @param vshct  Conversion time for shunt voltage.
 * @param vbusct Conversion time for bus voltage.
 * @param vtct   Conversion time for temperature.
 * @param avg    Averaging mode.
 */
#define INA237_ADC_CONFIG(mode, \
		      vshct, \
		      vbusct, \
		      vtct, \
		      avg)  \
	(((mode) << 12) | ((vbusct) << 9) | ((vshct) << 6) | ((vtct) << 3) | (avg))

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_INA237_H_ */
