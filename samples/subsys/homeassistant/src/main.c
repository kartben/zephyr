/*
 * Copyright (c) 2024 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/homeassistant/homeassistant.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>

LOG_MODULE_REGISTER(ha_sample, LOG_LEVEL_INF);

/* Network readiness */
static struct net_mgmt_event_callback net_mgmt_cb;
static K_SEM_DEFINE(network_ready, 0, 1);

/* Network event handler */
static void net_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			      struct net_if *iface)
{
	if (mgmt_event == NET_EVENT_L4_CONNECTED) {
		LOG_INF("Network connected! DHCP address assigned");
		k_sem_give(&network_ready);
	}
}

/* Wait for network to be ready with DHCP */
static void wait_for_network(void)
{
	net_mgmt_init_event_callback(&net_mgmt_cb, net_event_handler, NET_EVENT_L4_CONNECTED);
	net_mgmt_add_event_callback(&net_mgmt_cb);

	/* Trigger connection manager to start DHCP */
	conn_mgr_mon_resend_status();

	LOG_INF("Waiting for network (DHCP)...");
	k_sem_take(&network_ready, K_FOREVER);
}

/* Define Zbus channels - these are the data sources
 * Note: Using simple types (float, bool) that are automatically formatted
 * by the Home Assistant subsystem for MQTT publishing
 */
ZBUS_CHAN_DEFINE(temperature_chan, float, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));

ZBUS_CHAN_DEFINE(humidity_chan, float, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(0.0f));

ZBUS_CHAN_DEFINE(motion_chan, bool, NULL, NULL, ZBUS_OBSERVERS_EMPTY, ZBUS_MSG_INIT(false));

/* Register channels with Home Assistant */
static const struct homeassistant_channel_config temp_config = {
	.channel = &temperature_chan,
	.sensor_type = HOMEASSISTANT_SENSOR_TEMPERATURE,
	.friendly_name = "Living Room Temperature",
	.unit_of_measurement = "°C",
	.topic = NULL, /* Auto-generate topic */
	.value_template = NULL,
};

static const struct homeassistant_channel_config humidity_config = {
	.channel = &humidity_chan,
	.sensor_type = HOMEASSISTANT_SENSOR_HUMIDITY,
	.friendly_name = "Living Room Humidity",
	.unit_of_measurement = "%",
	.topic = NULL,
	.value_template = NULL,
};

static const struct homeassistant_channel_config motion_config = {
	.channel = &motion_chan,
	.sensor_type = HOMEASSISTANT_SENSOR_BINARY,
	.friendly_name = "Motion Sensor",
	.unit_of_measurement = NULL,
	.topic = NULL,
	.value_template = NULL,
};

/* Simulate sensor readings */
static void sensor_simulator_thread(void)
{
	float temp = 20.0f;
	float humidity = 50.0f;
	bool motion_state = false;
	int motion_counter = 0;

	LOG_INF("Sensor simulator started");

	while (1) {
		/* Simulate temperature changes */
		temp += ((float)sys_rand32_get() / UINT32_MAX - 0.5f) * 2.0f;
		if (temp < 15.0f) {
			temp = 15.0f;
		}
		if (temp > 30.0f) {
			temp = 30.0f;
		}

		zbus_chan_pub(&temperature_chan, &temp, K_SECONDS(1));

		LOG_INF("Temperature: %.1f °C", temp);

		k_sleep(K_SECONDS(5));

		/* Simulate humidity changes */
		humidity += ((float)sys_rand32_get() / UINT32_MAX - 0.5f) * 5.0f;
		if (humidity < 30.0f) {
			humidity = 30.0f;
		}
		if (humidity > 80.0f) {
			humidity = 80.0f;
		}

		zbus_chan_pub(&humidity_chan, &humidity, K_SECONDS(1));

		LOG_INF("Humidity: %.1f %%", humidity);

		k_sleep(K_SECONDS(5));

		/* Simulate motion detection - toggle every few cycles */
		motion_counter++;
		if (motion_counter >= 3) {
			motion_state = !motion_state;
			motion_counter = 0;

			zbus_chan_pub(&motion_chan, &motion_state, K_SECONDS(1));

			LOG_INF("Motion: %s", motion_state ? "DETECTED" : "CLEAR");
		}

		k_sleep(K_SECONDS(5));
	}
}

K_THREAD_DEFINE(sensor_sim_tid, 2048, sensor_simulator_thread, NULL, NULL, NULL, 7, 0, 0);

int main(void)
{
	int ret;

	LOG_INF("=================================================");
	LOG_INF("  Home Assistant Integration Sample");
	LOG_INF("=================================================");
	LOG_INF("");
	LOG_INF("This sample demonstrates easy Home Assistant");
	LOG_INF("integration using Zbus and MQTT.");
	LOG_INF("");
	LOG_INF("Simply publish to Zbus channels and they");
	LOG_INF("automatically appear in Home Assistant!");
	LOG_INF("");
	LOG_INF("MQTT Broker: %s:%d", CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME,
		CONFIG_HOMEASSISTANT_MQTT_BROKER_PORT);
	LOG_INF("Device Name: %s", CONFIG_HOMEASSISTANT_DEVICE_NAME);
	LOG_INF("");

	/* Wait for network to be ready (DHCP) */
	wait_for_network();

	/* Register channels with Home Assistant */
	ret = homeassistant_register_channel(&temp_config);
	if (ret) {
		LOG_ERR("Failed to register temperature channel: %d", ret);
		return ret;
	}

	ret = homeassistant_register_channel(&humidity_config);
	if (ret) {
		LOG_ERR("Failed to register humidity channel: %d", ret);
		return ret;
	}

	ret = homeassistant_register_channel(&motion_config);
	if (ret) {
		LOG_ERR("Failed to register motion channel: %d", ret);
		return ret;
	}

	LOG_INF("All channels registered successfully!");
	LOG_INF("");
	LOG_INF("Waiting for MQTT connection...");

	/* Wait a bit for connection */
	k_sleep(K_SECONDS(5));

	if (homeassistant_is_connected()) {
		LOG_INF("✓ Connected to Home Assistant!");
		LOG_INF("");
		LOG_INF("Check your Home Assistant UI for:");
		LOG_INF("  - Living Room Temperature");
		LOG_INF("  - Living Room Humidity");
		LOG_INF("  - Motion Sensor");
	} else {
		LOG_WRN("Not yet connected - will retry automatically");
	}

	LOG_INF("");
	LOG_INF("Publishing simulated sensor data...");
	LOG_INF("");

	return 0;
}
