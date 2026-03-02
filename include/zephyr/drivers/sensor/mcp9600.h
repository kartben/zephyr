/*
 * Copyright (c) 2025 Thomas Schmid <tom@lfence.de>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor API of MCP9600 sensor
 * @ingroup mcp9600_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_MCP9600H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_MCP9600H_

/**
 * @brief Microchip MCP9600 Thermocouple Electromotive Force (EMF) to °C Converter
 * @defgroup mcp9600_interface MCP9600
 * @ingroup sensor_interface_ext
 * @{
 */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <zephyr/drivers/sensor.h>

/**
 * @brief Custom sensor channels for MCP9600
 */
enum sensor_channel_mcp9600 {
	/** Read cold-junction temperature. */
	SENSOR_CHAN_MCP9600_COLD_JUNCTION_TEMP = SENSOR_CHAN_PRIV_START,
	/** Read hot-junction temperature. */
	SENSOR_CHAN_MCP9600_HOT_JUNCTION_TEMP,
	/** Read hot-to-cold junction temperature delta. */
	SENSOR_CHAN_MCP9600_DELTA_TEMP,
	/** Read raw ADC conversion value. */
	SENSOR_CHAN_MCP9600_RAW_ADC,
};

/**
 * @brief Custom sensor attributes for MCP9600
 */
enum sensor_attribute_mcp9600 {
	/** Set ADC resolution. */
	SENSOR_ATTR_MCP9600_ADC_RES = SENSOR_ATTR_PRIV_START,
	/** Set digital filter coefficient. */
	SENSOR_ATTR_MCP9600_FILTER_COEFFICIENT,
	/** Set thermocouple type. */
	SENSOR_ATTR_MCP9600_THERMOCOUPLE_TYPE,
	/** Set cold-junction resolution. */
	SENSOR_ATTR_MCP9600_COLD_JUNCTION_RESOLUTION,
	/** Read device identifier (read-only). */
	SENSOR_ATTR_MCP9600_DEV_ID,
};

/**
 * @name Thermocouple type selection
 * @brief Values for attribute SENSOR_CHAN_MCP9600_THERMOCOUPLE_TYPE
 * @{
 */
/** Select thermocouple type K. */
#define MCP9600_ATTR_VALUE_TYPE_K 0x0
/** Select thermocouple type J. */
#define MCP9600_ATTR_VALUE_TYPE_J 0x1
/** Select thermocouple type T. */
#define MCP9600_ATTR_VALUE_TYPE_T 0x2
/** Select thermocouple type N. */
#define MCP9600_ATTR_VALUE_TYPE_N 0x3
/** Select thermocouple type S. */
#define MCP9600_ATTR_VALUE_TYPE_S 0x4
/** Select thermocouple type E. */
#define MCP9600_ATTR_VALUE_TYPE_E 0x5
/** Select thermocouple type B. */
#define MCP9600_ATTR_VALUE_TYPE_B 0x6
/** Select thermocouple type R. */
#define MCP9600_ATTR_VALUE_TYPE_R 0x7
/** @} */


/**
 * @name ADC resolution
 * @brief MCP9600 values for attribute SENSOR_ATTR_MCP9600_ADC_RES
 * @{
 */
/** Select 18-bit ADC resolution. */
#define MCP9600_ATTR_VALUE_ADC_RES_18BIT 0x0
/** Select 16-bit ADC resolution. */
#define MCP9600_ATTR_VALUE_ADC_RES_16BIT 0x1
/** Select 14-bit ADC resolution. */
#define MCP9600_ATTR_VALUE_ADC_RES_14BIT 0x2
/** Select 12-bit ADC resolution. */
#define MCP9600_ATTR_VALUE_ADC_RES_12BIT 0x3
/**
 * @}
 */


/**
 * @name Cold junction temperature resolution
 * @brief MCP9600 values for attribute SENSOR_ATTR_MCP9600_COLD_JUNCTION_RESOLUTION
 * @{
 */
/** Select 0.0625 C cold-junction resolution. */
#define MCP9600_ATTR_VALUE_COLD_JUNC_TMP_RES_0_0625C 0x0
/** Select 0.25 C cold-junction resolution. */
#define MCP9600_ATTR_VALUE_COLD_JUNC_TMP_RES_0_25C   0x1
/**
 * @}
 */

#ifdef __cplusplus
}
#endif /* __cplusplus */

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_MCP9600H_ */
