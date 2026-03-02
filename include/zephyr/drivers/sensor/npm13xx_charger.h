/*
 * Copyright (c) 2023 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for nPM13xx charger status and attributes.
 * @ingroup npm13xx_charger_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM13XX_CHARGER_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM13XX_CHARGER_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief nPM13xx charger extensions.
 * @defgroup npm13xx_charger_interface nPM13xx Charger
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief nPM13xx charger-specific sensor channels.
 */
enum sensor_channel_npm13xx_charger {
	/** Report the charger state machine status. */
	SENSOR_CHAN_NPM13XX_CHARGER_STATUS = SENSOR_CHAN_PRIV_START,
	/** Report charger error flags. */
	SENSOR_CHAN_NPM13XX_CHARGER_ERROR,
	/** Report VBUS status flags. */
	SENSOR_CHAN_NPM13XX_CHARGER_VBUS_STATUS,
};

/**
 * @brief nPM13xx charger-specific sensor attributes.
 */
enum sensor_attribute_npm13xx_charger {
	/** Read whether VBUS is currently present. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_PRESENT = SENSOR_ATTR_PRIV_START,
	/** Configure the VBUS current limit. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_CUR_LIMIT,
	/** Configure VBUS overvoltage protection threshold. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_OVERVLT_PROT,
	/** Configure VBUS undervoltage threshold. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_UNDERVLT,
	/** Control VBUS suspend state. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_SUSPENDED,
	/** Control bus-out behavior. */
	SENSOR_ATTR_NPM13XX_CHARGER_VBUS_BUSOUT,
};

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_NPM13XX_CHARGER_H_ */
