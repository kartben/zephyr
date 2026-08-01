/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor channels and attributes of the nPM13xx PMIC charger.
 * @ingroup sensor_interface_ext_nordic
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM13XX_CHARGER_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM13XX_CHARGER_H_

#include <zephyr/drivers/sensor.h>

/** @brief nPM13xx charger specific channels */
enum sensor_channel_npm13xx_charger {
	/** Charger status register value */
	SENSOR_CHAN_NPM13XX_CHARGER_STATUS = SENSOR_CHAN_PRIV_START,
	/** Charger error reason register value */
	SENSOR_CHAN_NPM13XX_CHARGER_ERROR,
	/** VBUS status register value */
	SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
};

/**
 * @brief nPM13xx charger specific attributes
 *
 * Boolean VBUS status flags, read with the
 * SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS channel.
 */
enum sensor_attribute_npm13xx_charger {
	/** VBUS is present */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT = SENSOR_ATTR_PRIV_START,
	/** VBUS current limit active */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_CUR_LIMIT,
	/** VBUS overvoltage protection active */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_OVERVLT_PROT,
	/** VBUS undervoltage detected */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_UNDERVLT,
	/** VBUS suspended */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_SUSPENDED,
	/** VBUS output active */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_BUSOUT,
};

#endif
