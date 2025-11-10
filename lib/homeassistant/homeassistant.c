/*
 * Copyright (c) 2025
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/homeassistant.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
#include <zephyr/net/mqtt.h>
#endif

#include <string.h>
#include <stdio.h>
#include <errno.h>

LOG_MODULE_REGISTER(homeassistant, CONFIG_HOMEASSISTANT_LOG_LEVEL);

/** Home Assistant client internal structure */
struct homeassistant_client {
	/** Configuration */
	struct homeassistant_config config;
	/** MQTT client (if MQTT is enabled) */
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	struct mqtt_client mqtt_client;
	uint8_t mqtt_rx_buffer[256];
	uint8_t mqtt_tx_buffer[256];
	struct sockaddr_storage broker_addr;
	bool mqtt_connected;
#endif
	/** Registered entities count */
	uint32_t entity_count;
};

#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY

/**
 * @brief Get device class string for binary sensors
 */
static const char *get_binary_sensor_class_str(
	enum homeassistant_binary_sensor_class class)
{
	switch (class) {
	case HOMEASSISTANT_BINARY_SENSOR_BATTERY:
		return "battery";
	case HOMEASSISTANT_BINARY_SENSOR_DOOR:
		return "door";
	case HOMEASSISTANT_BINARY_SENSOR_GARAGE_DOOR:
		return "garage_door";
	case HOMEASSISTANT_BINARY_SENSOR_MOTION:
		return "motion";
	case HOMEASSISTANT_BINARY_SENSOR_OCCUPANCY:
		return "occupancy";
	case HOMEASSISTANT_BINARY_SENSOR_OPENING:
		return "opening";
	case HOMEASSISTANT_BINARY_SENSOR_WINDOW:
		return "window";
	default:
		return NULL;
	}
}

/**
 * @brief Get device class string for sensors
 */
static const char *get_sensor_class_str(enum homeassistant_sensor_class class)
{
	switch (class) {
	case HOMEASSISTANT_SENSOR_BATTERY:
		return "battery";
	case HOMEASSISTANT_SENSOR_HUMIDITY:
		return "humidity";
	case HOMEASSISTANT_SENSOR_ILLUMINANCE:
		return "illuminance";
	case HOMEASSISTANT_SENSOR_TEMPERATURE:
		return "temperature";
	case HOMEASSISTANT_SENSOR_PRESSURE:
		return "pressure";
	case HOMEASSISTANT_SENSOR_POWER:
		return "power";
	case HOMEASSISTANT_SENSOR_ENERGY:
		return "energy";
	case HOMEASSISTANT_SENSOR_VOLTAGE:
		return "voltage";
	case HOMEASSISTANT_SENSOR_CURRENT:
		return "current";
	default:
		return NULL;
	}
}

/**
 * @brief Get entity type string
 */
static const char *get_entity_type_str(enum homeassistant_entity_type type)
{
	switch (type) {
	case HOMEASSISTANT_ENTITY_BINARY_SENSOR:
		return "binary_sensor";
	case HOMEASSISTANT_ENTITY_SENSOR:
		return "sensor";
	case HOMEASSISTANT_ENTITY_SWITCH:
		return "switch";
	case HOMEASSISTANT_ENTITY_LIGHT:
		return "light";
	default:
		return "sensor";
	}
}

/**
 * @brief MQTT event handler
 */
static void mqtt_evt_handler(struct mqtt_client *const c,
			      const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		LOG_INF("MQTT connected");
		break;
	case MQTT_EVT_DISCONNECT:
		LOG_INF("MQTT disconnected");
		break;
	case MQTT_EVT_PUBLISH:
		LOG_DBG("MQTT publish received");
		break;
	default:
		break;
	}
}

/**
 * @brief Build MQTT discovery topic
 */
static int build_discovery_topic(const struct homeassistant_client *client,
				 const struct homeassistant_entity *entity,
				 char *topic, size_t topic_size)
{
	const char *prefix = client->config.discovery_prefix ?
				     client->config.discovery_prefix :
				     "homeassistant";
	const char *type_str = get_entity_type_str(entity->type);

	int ret = snprintf(topic, topic_size, "%s/%s/%s/%s/config",
			   prefix, type_str,
			   client->config.device.identifier,
			   entity->unique_id);

	if (ret < 0 || ret >= topic_size) {
		return -ENOMEM;
	}

	return 0;
}

/**
 * @brief Build MQTT discovery payload
 */
static int build_discovery_payload(const struct homeassistant_client *client,
				    const struct homeassistant_entity *entity,
				    char *payload, size_t payload_size)
{
	char *ptr = payload;
	size_t remaining = payload_size;
	int ret;

	/* Start JSON object */
	ret = snprintf(ptr, remaining, "{\"name\":\"%s\",\"unique_id\":\"%s\"",
		       entity->name, entity->unique_id);
	if (ret < 0 || ret >= remaining) {
		return -ENOMEM;
	}
	ptr += ret;
	remaining -= ret;

	/* Add state topic */
	if (entity->state_topic) {
		ret = snprintf(ptr, remaining, ",\"state_topic\":\"%s\"",
			       entity->state_topic);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	/* Add command topic for controllable entities */
	if (entity->command_topic &&
	    (entity->type == HOMEASSISTANT_ENTITY_SWITCH ||
	     entity->type == HOMEASSISTANT_ENTITY_LIGHT)) {
		ret = snprintf(ptr, remaining, ",\"command_topic\":\"%s\"",
			       entity->command_topic);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	/* Add device class */
	const char *class_str = NULL;
	if (entity->type == HOMEASSISTANT_ENTITY_BINARY_SENSOR) {
		class_str = get_binary_sensor_class_str(
			entity->binary_sensor_class);
	} else if (entity->type == HOMEASSISTANT_ENTITY_SENSOR) {
		class_str = get_sensor_class_str(entity->sensor_class);
	}

	if (class_str) {
		ret = snprintf(ptr, remaining, ",\"device_class\":\"%s\"",
			       class_str);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	/* Add unit of measurement for sensors */
	if (entity->unit_of_measurement &&
	    entity->type == HOMEASSISTANT_ENTITY_SENSOR) {
		ret = snprintf(ptr, remaining,
			       ",\"unit_of_measurement\":\"%s\"",
			       entity->unit_of_measurement);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	/* Add device information */
	ret = snprintf(ptr, remaining,
		       ",\"device\":{\"identifiers\":[\"%s\"],"
		       "\"name\":\"%s\"",
		       client->config.device.identifier,
		       client->config.device.name);
	if (ret < 0 || ret >= remaining) {
		return -ENOMEM;
	}
	ptr += ret;
	remaining -= ret;

	if (client->config.device.manufacturer) {
		ret = snprintf(ptr, remaining, ",\"manufacturer\":\"%s\"",
			       client->config.device.manufacturer);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	if (client->config.device.model) {
		ret = snprintf(ptr, remaining, ",\"model\":\"%s\"",
			       client->config.device.model);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	if (client->config.device.sw_version) {
		ret = snprintf(ptr, remaining, ",\"sw_version\":\"%s\"",
			       client->config.device.sw_version);
		if (ret < 0 || ret >= remaining) {
			return -ENOMEM;
		}
		ptr += ret;
		remaining -= ret;
	}

	/* Close device object and main object */
	ret = snprintf(ptr, remaining, "}}");
	if (ret < 0 || ret >= remaining) {
		return -ENOMEM;
	}

	return 0;
}

#endif /* CONFIG_HOMEASSISTANT_MQTT_DISCOVERY */

struct homeassistant_client *homeassistant_init(
	const struct homeassistant_config *config)
{
	if (!config || !config->device.name || !config->device.identifier) {
		LOG_ERR("Invalid configuration");
		return NULL;
	}

	struct homeassistant_client *client = k_malloc(sizeof(*client));

	if (!client) {
		LOG_ERR("Failed to allocate client memory");
		return NULL;
	}

	memset(client, 0, sizeof(*client));
	memcpy(&client->config, config, sizeof(*config));

	LOG_INF("Home Assistant client initialized for device: %s",
		config->device.name);

	return client;
}

int homeassistant_connect(struct homeassistant_client *client)
{
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	if (!client || !client->config.mqtt_broker) {
		return -EINVAL;
	}

	/* Initialize MQTT client */
	struct mqtt_client *mqtt = &client->mqtt_client;

	mqtt_client_init(mqtt);

	/* Setup broker address */
	struct sockaddr_in *broker = (struct sockaddr_in *)&client->broker_addr;

	broker->sin_family = AF_INET;
	broker->sin_port = htons(client->config.mqtt_port ?
					 client->config.mqtt_port : 1883);

	/* Resolve broker hostname */
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *result;

	int ret = zsock_getaddrinfo(client->config.mqtt_broker, NULL,
				     &hints, &result);
	if (ret != 0) {
		LOG_ERR("Failed to resolve broker address: %d", ret);
		return -EHOSTUNREACH;
	}

	memcpy(broker, result->ai_addr, result->ai_addrlen);
	zsock_freeaddrinfo(result);

	/* Setup MQTT client */
	mqtt->broker = (struct sockaddr *)&client->broker_addr;
	mqtt->evt_cb = mqtt_evt_handler;
	mqtt->client_id.utf8 = (uint8_t *)client->config.device.identifier;
	mqtt->client_id.size = strlen(client->config.device.identifier);
	mqtt->protocol_version = MQTT_VERSION_3_1_1;

	/* Setup buffers */
	mqtt->rx_buf = client->mqtt_rx_buffer;
	mqtt->rx_buf_size = sizeof(client->mqtt_rx_buffer);
	mqtt->tx_buf = client->mqtt_tx_buffer;
	mqtt->tx_buf_size = sizeof(client->mqtt_tx_buffer);

	/* Add credentials if provided */
	if (client->config.mqtt_username) {
		mqtt->user_name =
			(struct mqtt_utf8 *)&(struct mqtt_utf8){
				.utf8 = (uint8_t *)client->config.mqtt_username,
				.size = strlen(client->config.mqtt_username),
			};
	}

	if (client->config.mqtt_password) {
		mqtt->password =
			(struct mqtt_utf8 *)&(struct mqtt_utf8){
				.utf8 = (uint8_t *)client->config.mqtt_password,
				.size = strlen(client->config.mqtt_password),
			};
	}

	/* Connect to broker */
	ret = mqtt_connect(mqtt);
	if (ret != 0) {
		LOG_ERR("Failed to connect to MQTT broker: %d", ret);
		return ret;
	}

	client->mqtt_connected = true;
	LOG_INF("Connected to MQTT broker at %s:%d",
		client->config.mqtt_broker,
		client->config.mqtt_port ? client->config.mqtt_port : 1883);

	return 0;
#else
	LOG_WRN("MQTT discovery not enabled");
	return -ENOTSUP;
#endif
}

int homeassistant_register_entity(struct homeassistant_client *client,
				  const struct homeassistant_entity *entity)
{
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	if (!client || !entity || !entity->name || !entity->unique_id) {
		return -EINVAL;
	}

	if (!client->mqtt_connected) {
		LOG_ERR("MQTT not connected");
		return -ENOTCONN;
	}

	/* Build discovery topic */
	char topic[256];
	int ret = build_discovery_topic(client, entity, topic, sizeof(topic));

	if (ret < 0) {
		LOG_ERR("Failed to build discovery topic: %d", ret);
		return ret;
	}

	/* Build discovery payload */
	char payload[512];

	ret = build_discovery_payload(client, entity, payload, sizeof(payload));
	if (ret < 0) {
		LOG_ERR("Failed to build discovery payload: %d", ret);
		return ret;
	}

	LOG_DBG("Discovery topic: %s", topic);
	LOG_DBG("Discovery payload: %s", payload);

	/* Publish discovery message */
	struct mqtt_publish_param param = {
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message.topic.topic.utf8 = (uint8_t *)topic,
		.message.topic.topic.size = strlen(topic),
		.message.payload.data = payload,
		.message.payload.len = strlen(payload),
		.message_id = client->entity_count + 1,
		.dup_flag = 0,
		.retain_flag = 1,
	};

	ret = mqtt_publish(&client->mqtt_client, &param);
	if (ret < 0) {
		LOG_ERR("Failed to publish discovery message: %d", ret);
		return ret;
	}

	client->entity_count++;
	LOG_INF("Registered entity: %s (%s)", entity->name, entity->unique_id);

	return 0;
#else
	LOG_WRN("MQTT discovery not enabled");
	return -ENOTSUP;
#endif
}

int homeassistant_update_state(struct homeassistant_client *client,
			       const char *entity_id, const char *state)
{
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	if (!client || !entity_id || !state) {
		return -EINVAL;
	}

	if (!client->mqtt_connected) {
		LOG_ERR("MQTT not connected");
		return -ENOTCONN;
	}

	/* Use the state topic provided during entity registration */
	/* For simplicity, we assume the topic follows a standard pattern */
	char topic[256];
	int ret = snprintf(topic, sizeof(topic), "homeassistant/state/%s/%s",
			   client->config.device.identifier, entity_id);

	if (ret < 0 || ret >= sizeof(topic)) {
		return -ENOMEM;
	}

	/* Publish state update */
	struct mqtt_publish_param param = {
		.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.message.topic.topic.utf8 = (uint8_t *)topic,
		.message.topic.topic.size = strlen(topic),
		.message.payload.data = (uint8_t *)state,
		.message.payload.len = strlen(state),
		.message_id = 0,
		.dup_flag = 0,
		.retain_flag = 0,
	};

	ret = mqtt_publish(&client->mqtt_client, &param);
	if (ret < 0) {
		LOG_ERR("Failed to publish state update: %d", ret);
		return ret;
	}

	LOG_DBG("Updated state for %s: %s", entity_id, state);

	return 0;
#else
	LOG_WRN("MQTT discovery not enabled");
	return -ENOTSUP;
#endif
}

int homeassistant_process(struct homeassistant_client *client,
			  int32_t timeout_ms)
{
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	if (!client) {
		return -EINVAL;
	}

	if (!client->mqtt_connected) {
		return -ENOTCONN;
	}

	struct mqtt_client *mqtt = &client->mqtt_client;

	/* Process MQTT events */
	struct zsock_pollfd fds = {
		.fd = mqtt->transport.tcp.sock,
		.events = ZSOCK_POLLIN,
	};

	int ret = zsock_poll(&fds, 1, timeout_ms);

	if (ret < 0) {
		LOG_ERR("Poll error: %d", errno);
		return -errno;
	}

	if (ret > 0 && (fds.revents & ZSOCK_POLLIN)) {
		ret = mqtt_input(mqtt);
		if (ret < 0) {
			LOG_ERR("MQTT input error: %d", ret);
			return ret;
		}
	}

	/* Send keep-alive if needed */
	ret = mqtt_live(mqtt);
	if (ret < 0 && ret != -EAGAIN) {
		LOG_ERR("MQTT live error: %d", ret);
		return ret;
	}

	return 0;
#else
	LOG_WRN("MQTT discovery not enabled");
	return -ENOTSUP;
#endif
}

int homeassistant_disconnect(struct homeassistant_client *client)
{
#ifdef CONFIG_HOMEASSISTANT_MQTT_DISCOVERY
	if (!client) {
		return -EINVAL;
	}

	if (!client->mqtt_connected) {
		return 0;
	}

	int ret = mqtt_disconnect(&client->mqtt_client);

	if (ret < 0) {
		LOG_ERR("Failed to disconnect from MQTT: %d", ret);
		return ret;
	}

	client->mqtt_connected = false;
	LOG_INF("Disconnected from MQTT broker");

	return 0;
#else
	return 0;
#endif
}

void homeassistant_deinit(struct homeassistant_client *client)
{
	if (!client) {
		return;
	}

	homeassistant_disconnect(client);
	k_free(client);

	LOG_INF("Home Assistant client deinitialized");
}
