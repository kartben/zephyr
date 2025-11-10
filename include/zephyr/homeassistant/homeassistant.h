/*
 * Copyright (c) 2024 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_HOMEASSISTANT_H_
#define ZEPHYR_INCLUDE_HOMEASSISTANT_H_

#include <zephyr/kernel.h>
#include <zephyr/zbus/zbus.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Home Assistant Integration API
 * @defgroup homeassistant_apis Home Assistant APIs
 * @ingroup os_services
 * @{
 */

/**
 * @brief Sensor type for Home Assistant auto-discovery
 */
enum homeassistant_sensor_type {
	/** Generic sensor (no specific device class) */
	HOMEASSISTANT_SENSOR_GENERIC,
	/** Temperature sensor */
	HOMEASSISTANT_SENSOR_TEMPERATURE,
	/** Humidity sensor */
	HOMEASSISTANT_SENSOR_HUMIDITY,
	/** Pressure sensor */
	HOMEASSISTANT_SENSOR_PRESSURE,
	/** Battery level */
	HOMEASSISTANT_SENSOR_BATTERY,
	/** Illuminance sensor */
	HOMEASSISTANT_SENSOR_ILLUMINANCE,
	/** Power sensor */
	HOMEASSISTANT_SENSOR_POWER,
	/** Energy sensor */
	HOMEASSISTANT_SENSOR_ENERGY,
	/** Voltage sensor */
	HOMEASSISTANT_SENSOR_VOLTAGE,
	/** Current sensor */
	HOMEASSISTANT_SENSOR_CURRENT,
	/** Binary sensor (on/off) */
	HOMEASSISTANT_SENSOR_BINARY,
};

/**
 * @brief Channel configuration for Home Assistant integration
 */
struct homeassistant_channel_config {
	/** The Zbus channel to monitor */
	const struct zbus_channel *channel;
	/** Sensor type for Home Assistant discovery */
	enum homeassistant_sensor_type sensor_type;
	/** Friendly name for the sensor (used in Home Assistant UI) */
	const char *friendly_name;
	/** Unit of measurement (e.g., "°C", "%", "hPa") */
	const char *unit_of_measurement;
	/** Custom MQTT topic (if NULL, auto-generated from channel name) */
	const char *topic;
	/** Value template for parsing the message (NULL for raw value) */
	const char *value_template;
};

/**
 * @brief Register a Zbus channel to be published to Home Assistant via MQTT
 *
 * This function registers a Zbus channel so that whenever data is published
 * to the channel, it's automatically forwarded to Home Assistant via MQTT.
 *
 * @param config Channel configuration
 * @return 0 on success, negative errno on failure
 */
int homeassistant_register_channel(const struct homeassistant_channel_config *config);

/**
 * @brief Unregister a Zbus channel from Home Assistant integration
 *
 * @param channel The channel to unregister
 * @return 0 on success, negative errno on failure
 */
int homeassistant_unregister_channel(const struct zbus_channel *channel);

/**
 * @brief Get the current connection status
 *
 * @return true if connected to MQTT broker, false otherwise
 */
bool homeassistant_is_connected(void);

/**
 * @brief Manually publish a value to a custom Home Assistant topic
 *
 * This allows publishing arbitrary data to MQTT topics without using Zbus channels.
 *
 * @param topic The MQTT topic to publish to
 * @param payload The payload to publish
 * @param payload_len Length of the payload
 * @return 0 on success, negative errno on failure
 */
int homeassistant_publish(const char *topic, const void *payload, size_t payload_len);

/**
 * @brief Helper macro to register a simple numeric sensor
 *
 * Example:
 * @code
 * ZBUS_CHAN_DEFINE(temp_chan, float, ...);
 * HOMEASSISTANT_REGISTER_SENSOR(temp_chan, TEMPERATURE, "Living Room Temperature", "°C");
 * @endcode
 */
#define HOMEASSISTANT_REGISTER_SENSOR(_channel, _type, _name, _unit)                               \
	static const struct homeassistant_channel_config _channel##_ha_config = {                  \
		.channel = &_channel,                                                              \
		.sensor_type = HOMEASSISTANT_SENSOR_##_type,                                       \
		.friendly_name = _name,                                                            \
		.unit_of_measurement = _unit,                                                      \
		.topic = NULL,                                                                     \
		.value_template = NULL,                                                            \
	};                                                                                         \
	static int _channel##_ha_init(void)                                                        \
	{                                                                                          \
		return homeassistant_register_channel(&_channel##_ha_config);                      \
	}                                                                                          \
	SYS_INIT(_channel##_ha_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY)

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_HOMEASSISTANT_H_ */
