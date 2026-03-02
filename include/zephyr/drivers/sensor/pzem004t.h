/*
 * Copyright (c) 2025 Srishtik Bhandarkar
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for PZEM004T measurements and controls.
 * @ingroup pzem004t_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_PZEM004T_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_PZEM004T_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief PZEM004T sensor extensions.
 * @defgroup pzem004t_interface PZEM004T
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief PZEM004T-specific sensor channels.
 */
enum sensor_channel_pzem004t {
	/** Energy corresponds to active power accumulated over time.
	 * Units: 1Wh (Watt-hours)
	 */
	SENSOR_CHAN_PZEM004T_ENERGY = SENSOR_CHAN_PRIV_START,
	/** Power factor is defined as the ratio of real power to apparent power,
	 * with 0.01 resolution.
	 * Unit: No unit
	 */
	SENSOR_CHAN_PZEM004T_POWER_FACTOR,
	/** Alarm status is 0xFF when current active power is greater than power alarm threshold.
	 * 0x00 when current power is less than power alarm threshold.
	 * Unit: No unit
	 */
	SENSOR_CHAN_PZEM004T_ALARM_STATUS,
	/** Set active power threshold for power alarm.
	 * Unit: 1W (Watts)
	 */
	SENSOR_CHAN_PZEM004T_POWER_ALARM_THRESHOLD,
	/** Get or set the unique Modbus RTU address on the bus. Only use this
	 * to set individual modbus address by connecteing each device individually.
	 */
	SENSOR_CHAN_PZEM004T_MODBUS_RTU_ADDRESS,
	/** Set the Modbus address used by the driver instance.
	 * This does not set the modbus address of the device. It is used to set the
	 * modbus address of the device instance only for the driver.
	 */
	SENSOR_CHAN_PZEM004T_ADDRESS_INST_SET,
	/** Reset the accumulated energy counter.
	 * Enable CONFIG_PZEM004T_ENABLE_RESET_ENERGY in the application configuration
	 * to use this channel.
	 */
	SENSOR_CHAN_PZEM004T_RESET_ENERGY,
};

/**
 * @brief PZEM004T-specific sensor attributes.
 */
enum sensor_attribute_pzem004t {
	/** Set active power threshold for power alarm.
	 * Unit: 1W (Watts)
	 */
	SENSOR_ATTR_PZEM004T_POWER_ALARM_THRESHOLD = SENSOR_ATTR_PRIV_START,
	/** Get or set the unique Modbus RTU address on the bus. Only use this
	 * to set individual modbus address by connecteing each device individually.
	 */
	SENSOR_ATTR_PZEM004T_MODBUS_RTU_ADDRESS,
	/** Set the Modbus address used by the driver instance.
	 * This does not set the modbus address of the device. It is used to set the
	 * modbus address of the device instance only for the driver.
	 */
	SENSOR_ATTR_PZEM004T_ADDRESS_INST_SET,
	/** Reset the accumulated energy counter.
	 * Enable CONFIG_PZEM004T_ENABLE_RESET_ENERGY in the application configuration
	 * to use this attribute.
	 */
	SENSOR_ATTR_PZEM004T_RESET_ENERGY,
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_PZEM004T_H_ */
