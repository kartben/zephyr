/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/homeassistant.h>
#include <stdio.h>

LOG_MODULE_REGISTER(app, LOG_LEVEL_INF);

/* MQTT broker configuration - can be overridden via Kconfig */
#define MQTT_BROKER CONFIG_HOMEASSISTANT_MQTT_BROKER
#define MQTT_PORT CONFIG_HOMEASSISTANT_MQTT_PORT

/* Simulated temperature sensor */
static float current_temperature = 22.5f;

static void update_temperature(void)
{
	/* Simulate temperature changes */
	current_temperature += ((float)(sys_rand32_get() % 20) - 10.0f) / 10.0f;

	/* Keep in reasonable range */
	if (current_temperature < 15.0f) {
		current_temperature = 15.0f;
	} else if (current_temperature > 30.0f) {
		current_temperature = 30.0f;
	}
}

int main(void)
{
	int ret;

	LOG_INF("Home Assistant Integration Sample");

	/* Wait for network to be ready */
	k_sleep(K_SECONDS(2));

	/* Configure Home Assistant client */
	struct homeassistant_config config = {
		.mqtt_broker = MQTT_BROKER,
		.mqtt_port = MQTT_PORT,
		.mqtt_username = NULL,
		.mqtt_password = NULL,
		.discovery_prefix = "homeassistant",
		.device = {
			.name = "Zephyr Device",
			.manufacturer = "Zephyr Project",
			.model = "Sample Device",
			.sw_version = "1.0.0",
			.identifier = "zephyr_device_001",
		},
	};

	/* Initialize Home Assistant client */
	struct homeassistant_client *client = homeassistant_init(&config);

	if (!client) {
		LOG_ERR("Failed to initialize Home Assistant client");
		return -1;
	}

	/* Connect to MQTT broker */
	ret = homeassistant_connect(client);
	if (ret < 0) {
		LOG_ERR("Failed to connect to Home Assistant: %d", ret);
		homeassistant_deinit(client);
		return -1;
	}

	/* Wait for connection to establish */
	k_sleep(K_MSEC(500));

	/* Register a temperature sensor entity */
	struct homeassistant_entity temp_sensor = {
		.type = HOMEASSISTANT_ENTITY_SENSOR,
		.name = "Temperature Sensor",
		.unique_id = "temp_sensor",
		.sensor_class = HOMEASSISTANT_SENSOR_TEMPERATURE,
		.unit_of_measurement = "°C",
		.state_topic = "homeassistant/state/zephyr_device_001/temp_sensor",
		.command_topic = NULL,
	};

	ret = homeassistant_register_entity(client, &temp_sensor);
	if (ret < 0) {
		LOG_ERR("Failed to register temperature sensor: %d", ret);
	}

	/* Main loop - update temperature every 5 seconds */
	while (1) {
		char state_str[32];

		update_temperature();

		snprintf(state_str, sizeof(state_str), "%.1f",
			 current_temperature);

		ret = homeassistant_update_state(client, "temp_sensor",
						 state_str);
		if (ret < 0) {
			LOG_ERR("Failed to update state: %d", ret);
		} else {
			LOG_INF("Temperature: %.1f°C", current_temperature);
		}

		/* Process MQTT events */
		homeassistant_process(client, 100);

		k_sleep(K_SECONDS(5));
	}

	/* Cleanup (never reached in this sample) */
	homeassistant_disconnect(client);
	homeassistant_deinit(client);

	return 0;
}
