/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_HOMEASSISTANT_H_
#define ZEPHYR_INCLUDE_HOMEASSISTANT_H_

/**
 * @file
 * @brief Home Assistant Integration API
 *
 * This file contains the API for integrating Zephyr-based IoT devices
 * with Home Assistant using MQTT discovery protocol and REST API.
 */

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup homeassistant Home Assistant Integration
 * @ingroup networking
 * @{
 */

/** Home Assistant entity types */
enum homeassistant_entity_type {
	/** Binary sensor (on/off, true/false) */
	HOMEASSISTANT_ENTITY_BINARY_SENSOR,
	/** Sensor (numeric or string value) */
	HOMEASSISTANT_ENTITY_SENSOR,
	/** Switch (controllable on/off device) */
	HOMEASSISTANT_ENTITY_SWITCH,
	/** Light (controllable light) */
	HOMEASSISTANT_ENTITY_LIGHT,
};

/** Home Assistant device class for binary sensors */
enum homeassistant_binary_sensor_class {
	HOMEASSISTANT_BINARY_SENSOR_NONE,
	HOMEASSISTANT_BINARY_SENSOR_BATTERY,
	HOMEASSISTANT_BINARY_SENSOR_DOOR,
	HOMEASSISTANT_BINARY_SENSOR_GARAGE_DOOR,
	HOMEASSISTANT_BINARY_SENSOR_MOTION,
	HOMEASSISTANT_BINARY_SENSOR_OCCUPANCY,
	HOMEASSISTANT_BINARY_SENSOR_OPENING,
	HOMEASSISTANT_BINARY_SENSOR_WINDOW,
};

/** Home Assistant device class for sensors */
enum homeassistant_sensor_class {
	HOMEASSISTANT_SENSOR_NONE,
	HOMEASSISTANT_SENSOR_BATTERY,
	HOMEASSISTANT_SENSOR_HUMIDITY,
	HOMEASSISTANT_SENSOR_ILLUMINANCE,
	HOMEASSISTANT_SENSOR_TEMPERATURE,
	HOMEASSISTANT_SENSOR_PRESSURE,
	HOMEASSISTANT_SENSOR_POWER,
	HOMEASSISTANT_SENSOR_ENERGY,
	HOMEASSISTANT_SENSOR_VOLTAGE,
	HOMEASSISTANT_SENSOR_CURRENT,
};

/** Home Assistant device information */
struct homeassistant_device {
	/** Device name */
	const char *name;
	/** Device manufacturer */
	const char *manufacturer;
	/** Device model */
	const char *model;
	/** Device software version */
	const char *sw_version;
	/** Device unique identifier */
	const char *identifier;
};

/** Home Assistant entity configuration */
struct homeassistant_entity {
	/** Entity type */
	enum homeassistant_entity_type type;
	/** Entity name */
	const char *name;
	/** Entity unique ID */
	const char *unique_id;
	/** Device class (type-specific) */
	union {
		enum homeassistant_binary_sensor_class binary_sensor_class;
		enum homeassistant_sensor_class sensor_class;
	};
	/** Unit of measurement (for sensors) */
	const char *unit_of_measurement;
	/** State topic for MQTT */
	const char *state_topic;
	/** Command topic for MQTT (for controllable entities) */
	const char *command_topic;
};

/** Home Assistant client configuration */
struct homeassistant_config {
	/** MQTT broker hostname */
	const char *mqtt_broker;
	/** MQTT broker port */
	uint16_t mqtt_port;
	/** MQTT username (optional, can be NULL) */
	const char *mqtt_username;
	/** MQTT password (optional, can be NULL) */
	const char *mqtt_password;
	/** MQTT discovery prefix (default: "homeassistant") */
	const char *discovery_prefix;
	/** Home Assistant REST API URL (optional) */
	const char *api_url;
	/** Home Assistant API token (optional) */
	const char *api_token;
	/** Device information */
	struct homeassistant_device device;
};

/** Home Assistant client handle */
struct homeassistant_client;

/**
 * @brief Initialize Home Assistant client
 *
 * @param config Configuration for the Home Assistant client
 * @return Pointer to initialized client, or NULL on error
 */
struct homeassistant_client *homeassistant_init(
	const struct homeassistant_config *config);

/**
 * @brief Register an entity with Home Assistant
 *
 * Publishes MQTT discovery message for the entity so that
 * Home Assistant can automatically detect and configure it.
 *
 * @param client Home Assistant client
 * @param entity Entity configuration to register
 * @return 0 on success, negative errno on error
 */
int homeassistant_register_entity(
	struct homeassistant_client *client,
	const struct homeassistant_entity *entity);

/**
 * @brief Update entity state
 *
 * Publishes the current state of an entity to Home Assistant.
 *
 * @param client Home Assistant client
 * @param entity_id Entity unique ID
 * @param state State value (string representation)
 * @return 0 on success, negative errno on error
 */
int homeassistant_update_state(
	struct homeassistant_client *client,
	const char *entity_id,
	const char *state);

/**
 * @brief Connect to Home Assistant MQTT broker
 *
 * Establishes connection to the MQTT broker for entity discovery
 * and state updates.
 *
 * @param client Home Assistant client
 * @return 0 on success, negative errno on error
 */
int homeassistant_connect(struct homeassistant_client *client);

/**
 * @brief Disconnect from Home Assistant
 *
 * @param client Home Assistant client
 * @return 0 on success, negative errno on error
 */
int homeassistant_disconnect(struct homeassistant_client *client);

/**
 * @brief Process Home Assistant client events
 *
 * Should be called periodically to handle MQTT messages and
 * maintain connection.
 *
 * @param client Home Assistant client
 * @param timeout_ms Timeout in milliseconds
 * @return 0 on success, negative errno on error
 */
int homeassistant_process(struct homeassistant_client *client,
			  int32_t timeout_ms);

/**
 * @brief Deinitialize Home Assistant client
 *
 * Frees resources allocated for the client.
 *
 * @param client Home Assistant client
 */
void homeassistant_deinit(struct homeassistant_client *client);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_HOMEASSISTANT_H_ */
