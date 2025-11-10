/*
 * Copyright (c) 2025 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_MGMT_HOMEASSISTANT_H_
#define ZEPHYR_INCLUDE_MGMT_HOMEASSISTANT_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Home Assistant Integration API
 * @defgroup homeassistant_api Home Assistant Integration API
 * @ingroup mgmt
 * @{
 */

/**
 * @brief Entity types supported by Home Assistant
 */
enum homeassistant_entity_type {
	/** Sensor entity (read-only) */
	HOMEASSISTANT_ENTITY_SENSOR,
	/** Binary sensor entity (on/off) */
	HOMEASSISTANT_ENTITY_BINARY_SENSOR,
	/** Switch entity (controllable on/off) */
	HOMEASSISTANT_ENTITY_SWITCH,
	/** Number entity (numeric value) */
	HOMEASSISTANT_ENTITY_NUMBER,
};

/**
 * @brief Home Assistant entity configuration
 */
struct homeassistant_entity_config {
	/** Name of the entity as it appears in Home Assistant */
	const char *name;
	/** Type of entity */
	enum homeassistant_entity_type type;
	/** Unit of measurement (for sensors, can be NULL) */
	const char *unit;
	/** Device class (e.g., "temperature", "humidity", can be NULL) */
	const char *device_class;
	/** Zbus channel associated with this entity */
	const struct zbus_channel *channel;
};

/**
 * @brief Register a Zbus channel with Home Assistant
 *
 * This function registers a Zbus channel to be automatically synchronized
 * with Home Assistant. The channel data will be published to Home Assistant
 * as the specified entity type.
 *
 * @param config Entity configuration
 * @return 0 on success, negative errno on failure
 */
int homeassistant_register_entity(const struct homeassistant_entity_config *config);

/**
 * @brief Initialize Home Assistant integration
 *
 * This function starts the Home Assistant integration service, which monitors
 * registered Zbus channels and synchronizes them with Home Assistant.
 *
 * @return 0 on success, negative errno on failure
 */
int homeassistant_init(void);

/**
 * @brief Macro to define and register a Home Assistant entity
 *
 * This macro creates a Home Assistant entity configuration and automatically
 * registers it at initialization time.
 *
 * @param _name Name of the entity
 * @param _type Entity type (e.g., HOMEASSISTANT_ENTITY_SENSOR)
 * @param _unit Unit of measurement (or NULL)
 * @param _device_class Device class (or NULL)
 * @param _channel Zbus channel to monitor
 */
#define HOMEASSISTANT_ENTITY_DEFINE(_name, _type, _unit, _device_class, _channel) \
	static const struct homeassistant_entity_config _homeassistant_entity_##_name = { \
		.name = STRINGIFY(_name), \
		.type = _type, \
		.unit = _unit, \
		.device_class = _device_class, \
		.channel = &_channel, \
	}; \
	static int _homeassistant_entity_init_##_name(void) \
	{ \
		return homeassistant_register_entity(&_homeassistant_entity_##_name); \
	} \
	SYS_INIT(_homeassistant_entity_init_##_name, APPLICATION, 91)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_MGMT_HOMEASSISTANT_H_ */
