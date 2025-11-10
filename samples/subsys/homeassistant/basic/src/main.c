/*
 * Copyright (c) 2025 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/mgmt/homeassistant/homeassistant.h>

LOG_MODULE_REGISTER(ha_sample, LOG_LEVEL_INF);

/**
 * @brief Temperature sensor data structure
 */
struct temperature_msg {
	int value;
};

/**
 * @brief Humidity sensor data structure
 */
struct humidity_msg {
	int value;
};

/**
 * @brief Light switch data structure
 */
struct switch_msg {
	int state;
};

/**
 * Define Zbus channels for sensor data
 */
ZBUS_CHAN_DEFINE(temperature_chan,
		 struct temperature_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.value = 20)
);

ZBUS_CHAN_DEFINE(humidity_chan,
		 struct humidity_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.value = 50)
);

ZBUS_CHAN_DEFINE(light_switch_chan,
		 struct switch_msg,
		 NULL,
		 NULL,
		 ZBUS_OBSERVERS_EMPTY,
		 ZBUS_MSG_INIT(.state = 0)
);

/**
 * Register Home Assistant entities using the convenient macro
 */
HOMEASSISTANT_ENTITY_DEFINE(temperature,
			    HOMEASSISTANT_ENTITY_SENSOR,
			    "°C",
			    "temperature",
			    temperature_chan);

HOMEASSISTANT_ENTITY_DEFINE(humidity,
			    HOMEASSISTANT_ENTITY_SENSOR,
			    "%",
			    "humidity",
			    humidity_chan);

HOMEASSISTANT_ENTITY_DEFINE(light,
			    HOMEASSISTANT_ENTITY_SWITCH,
			    NULL,
			    "switch",
			    light_switch_chan);

/**
 * @brief Simulate sensor readings
 */
static void simulate_sensors(void)
{
	struct temperature_msg temp;
	struct humidity_msg hum;
	struct switch_msg sw;
	int counter = 0;

	LOG_INF("Starting sensor simulation...");

	while (1) {
		/* Simulate temperature changes (20-30°C) */
		temp.value = 20 + (counter % 10);
		zbus_chan_pub(&temperature_chan, &temp, K_MSEC(100));
		LOG_INF("Temperature: %d°C", temp.value);

		/* Simulate humidity changes (40-60%) */
		hum.value = 40 + ((counter * 2) % 20);
		zbus_chan_pub(&humidity_chan, &hum, K_MSEC(100));
		LOG_INF("Humidity: %d%%", hum.value);

		/* Toggle light switch every 10 iterations */
		if (counter % 10 == 0) {
			sw.state = !sw.state;
			zbus_chan_pub(&light_switch_chan, &sw, K_MSEC(100));
			LOG_INF("Light switch: %s", sw.state ? "ON" : "OFF");
		}

		counter++;

		/* Wait before next reading */
		k_sleep(K_SECONDS(3));
	}
}

int main(void)
{
	LOG_INF("Home Assistant Integration Sample");
	LOG_INF("===================================");
	LOG_INF("This sample demonstrates Zbus integration with Home Assistant");
	LOG_INF("Sensor data is automatically published to Home Assistant");
	LOG_INF("");
	LOG_INF("Configuration:");
	LOG_INF("  Server: %s:%d", CONFIG_HOMEASSISTANT_SERVER_ADDR,
		CONFIG_HOMEASSISTANT_SERVER_PORT);
	LOG_INF("  Update interval: %d ms", CONFIG_HOMEASSISTANT_UPDATE_INTERVAL);
	LOG_INF("");

	/* Start sensor simulation */
	simulate_sensors();

	return 0;
}
