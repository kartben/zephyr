/*
 * Copyright (c) 2024 Benjamin Cabé
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/homeassistant/homeassistant.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/random/random.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(homeassistant, CONFIG_HOMEASSISTANT_LOG_LEVEL);

/* MQTT client context */
static struct mqtt_client client;
static uint8_t rx_buffer[CONFIG_HOMEASSISTANT_MQTT_BUFFER_SIZE];
static uint8_t tx_buffer[CONFIG_HOMEASSISTANT_MQTT_BUFFER_SIZE];
static struct sockaddr_storage broker;
static struct zsock_pollfd fds[1];
static bool mqtt_connected;

/* Channel registry */
struct channel_entry {
	const struct homeassistant_channel_config *config;
	bool in_use;
};

static struct channel_entry channel_registry[CONFIG_HOMEASSISTANT_MAX_CHANNELS];
static K_MUTEX_DEFINE(registry_mutex);

/* Forward declarations */
static void mqtt_evt_handler(struct mqtt_client *mqtt_client, const struct mqtt_evt *evt);
static int publish_discovery(const struct homeassistant_channel_config *config);
static void channel_listener_cb(const struct zbus_channel *chan);

/* Define a single listener for all Home Assistant channels */
ZBUS_LISTENER_DEFINE(ha_listener, channel_listener_cb);

/* Get sensor device class string */
static const char *get_device_class(enum homeassistant_sensor_type type)
{
	switch (type) {
	case HOMEASSISTANT_SENSOR_TEMPERATURE:
		return "temperature";
	case HOMEASSISTANT_SENSOR_HUMIDITY:
		return "humidity";
	case HOMEASSISTANT_SENSOR_PRESSURE:
		return "pressure";
	case HOMEASSISTANT_SENSOR_BATTERY:
		return "battery";
	case HOMEASSISTANT_SENSOR_ILLUMINANCE:
		return "illuminance";
	case HOMEASSISTANT_SENSOR_POWER:
		return "power";
	case HOMEASSISTANT_SENSOR_ENERGY:
		return "energy";
	case HOMEASSISTANT_SENSOR_VOLTAGE:
		return "voltage";
	case HOMEASSISTANT_SENSOR_CURRENT:
		return "current";
	case HOMEASSISTANT_SENSOR_BINARY:
		return "binary_sensor";
	default:
		return NULL;
	}
}

/* MQTT event handler */
static void mqtt_evt_handler(struct mqtt_client *mqtt_client, const struct mqtt_evt *evt)
{
	switch (evt->type) {
	case MQTT_EVT_CONNACK:
		if (evt->result == 0) {
			LOG_INF("Connected to MQTT broker");
			mqtt_connected = true;

			/* Publish discovery messages for all registered channels */
			k_mutex_lock(&registry_mutex, K_FOREVER);
			for (int i = 0; i < CONFIG_HOMEASSISTANT_MAX_CHANNELS; i++) {
				if (channel_registry[i].in_use) {
					publish_discovery(channel_registry[i].config);
				}
			}
			k_mutex_unlock(&registry_mutex);
		} else {
			LOG_ERR("Failed to connect: %d", evt->result);
			mqtt_connected = false;
		}
		break;

	case MQTT_EVT_DISCONNECT:
		LOG_INF("Disconnected from MQTT broker");
		mqtt_connected = false;
		break;

	case MQTT_EVT_PUBACK:
		LOG_DBG("PUBACK packet id: %u", evt->param.puback.message_id);
		break;

	default:
		break;
	}
}

/* Initialize MQTT client */
static int ha_mqtt_client_init(void)
{
	struct sockaddr_in *broker4 = (struct sockaddr_in *)&broker;

	broker4->sin_family = AF_INET;
	broker4->sin_port = htons(CONFIG_HOMEASSISTANT_MQTT_BROKER_PORT);

	/* Resolve hostname */
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *addr_info;

	int ret = zsock_getaddrinfo(CONFIG_HOMEASSISTANT_MQTT_BROKER_HOSTNAME, NULL, &hints,
				    &addr_info);
	if (ret != 0) {
		LOG_ERR("Failed to resolve hostname: %d", ret);
		return ret;
	}

	memcpy(&broker4->sin_addr, &((struct sockaddr_in *)addr_info->ai_addr)->sin_addr,
	       sizeof(struct in_addr));

	zsock_freeaddrinfo(addr_info);

	mqtt_client_init(&client);

	client.broker = &broker;
	client.evt_cb = mqtt_evt_handler;
	client.client_id.utf8 = (uint8_t *)CONFIG_HOMEASSISTANT_MQTT_CLIENT_ID;
	client.client_id.size = strlen(CONFIG_HOMEASSISTANT_MQTT_CLIENT_ID);
	client.protocol_version = MQTT_VERSION_3_1_1;
	client.rx_buf = rx_buffer;
	client.rx_buf_size = sizeof(rx_buffer);
	client.tx_buf = tx_buffer;
	client.tx_buf_size = sizeof(tx_buffer);

	/* Set username and password if configured */
	static struct mqtt_utf8 user_name;
	static struct mqtt_utf8 password;

	if (strlen(CONFIG_HOMEASSISTANT_MQTT_USERNAME) > 0) {
		user_name.utf8 = (uint8_t *)CONFIG_HOMEASSISTANT_MQTT_USERNAME;
		user_name.size = strlen(CONFIG_HOMEASSISTANT_MQTT_USERNAME);
		client.user_name = &user_name;
	}

	if (strlen(CONFIG_HOMEASSISTANT_MQTT_PASSWORD) > 0) {
		password.utf8 = (uint8_t *)CONFIG_HOMEASSISTANT_MQTT_PASSWORD;
		password.size = strlen(CONFIG_HOMEASSISTANT_MQTT_PASSWORD);
		client.password = &password;
	}

	return 0;
}

/* Connect to MQTT broker */
static int ha_mqtt_connect(void)
{
	int ret;

	ret = mqtt_connect(&client);
	if (ret) {
		LOG_ERR("Failed to connect: %d", ret);
		return ret;
	}

	fds[0].fd = client.transport.tcp.sock;
	fds[0].events = ZSOCK_POLLIN;

	return 0;
}

/* Publish Home Assistant discovery message */
static int publish_discovery(const struct homeassistant_channel_config *config)
{
	if (!IS_ENABLED(CONFIG_HOMEASSISTANT_AUTO_DISCOVERY)) {
		return 0;
	}

	if (!mqtt_connected) {
		return -ENOTCONN;
	}

	char discovery_topic[128];
	char discovery_payload[512];
	char state_topic[128];
	const char *chan_name = zbus_chan_name(config->channel);
	const char *device_class = get_device_class(config->sensor_type);

	/* Generate state topic */
	if (config->topic) {
		snprintf(state_topic, sizeof(state_topic), "%s", config->topic);
	} else {
		snprintf(state_topic, sizeof(state_topic), "%s/%s/state",
			 CONFIG_HOMEASSISTANT_DEVICE_NAME, chan_name);
	}

	/* Generate discovery topic */
	const char *component =
		(config->sensor_type == HOMEASSISTANT_SENSOR_BINARY) ? "binary_sensor" : "sensor";
	snprintf(discovery_topic, sizeof(discovery_topic), "%s/%s/%s_%s/config",
		 CONFIG_HOMEASSISTANT_DISCOVERY_PREFIX, component, CONFIG_HOMEASSISTANT_DEVICE_NAME,
		 chan_name);

	/* Build discovery payload */
	int len = snprintf(discovery_payload, sizeof(discovery_payload),
			   "{\"name\":\"%s\",\"state_topic\":\"%s\"",
			   config->friendly_name ? config->friendly_name : chan_name, state_topic);

	if (device_class) {
		len += snprintf(discovery_payload + len, sizeof(discovery_payload) - len,
				",\"device_class\":\"%s\"", device_class);
	}

	if (config->unit_of_measurement) {
		len += snprintf(discovery_payload + len, sizeof(discovery_payload) - len,
				",\"unit_of_measurement\":\"%s\"", config->unit_of_measurement);
	}

	if (config->value_template) {
		len += snprintf(discovery_payload + len, sizeof(discovery_payload) - len,
				",\"value_template\":\"%s\"", config->value_template);
	}

	/* Add device info */
	len += snprintf(discovery_payload + len, sizeof(discovery_payload) - len,
			",\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\"}}",
			CONFIG_HOMEASSISTANT_DEVICE_NAME, CONFIG_HOMEASSISTANT_DEVICE_NAME);

	struct mqtt_publish_param param = {
		.message.topic.qos = MQTT_QOS_1_AT_LEAST_ONCE,
		.message.topic.topic.utf8 = (uint8_t *)discovery_topic,
		.message.topic.topic.size = strlen(discovery_topic),
		.message.payload.data = discovery_payload,
		.message.payload.len = len,
		.message_id = sys_rand32_get(),
		.dup_flag = 0,
		.retain_flag = 1,
	};

	LOG_DBG("Publishing discovery: %s", discovery_topic);
	LOG_DBG("Payload: %s", discovery_payload);

	int ret = mqtt_publish(&client, &param);
	if (ret) {
		LOG_ERR("Failed to publish discovery: %d", ret);
		return ret;
	}

	return 0;
}

/* Zbus listener callback - called when data is published to a monitored channel */
static void channel_listener_cb(const struct zbus_channel *chan)
{
	if (!mqtt_connected) {
		return;
	}

	/* Find the channel configuration */
	const struct homeassistant_channel_config *config = NULL;

	k_mutex_lock(&registry_mutex, K_FOREVER);
	for (int i = 0; i < CONFIG_HOMEASSISTANT_MAX_CHANNELS; i++) {
		if (channel_registry[i].in_use && channel_registry[i].config->channel == chan) {
			config = channel_registry[i].config;
			break;
		}
	}
	k_mutex_unlock(&registry_mutex);

	if (!config) {
		return;
	}

	/* Generate MQTT topic */
	char topic[128];
	if (config->topic) {
		snprintf(topic, sizeof(topic), "%s", config->topic);
	} else {
		snprintf(topic, sizeof(topic), "%s/%s/state", CONFIG_HOMEASSISTANT_DEVICE_NAME,
			 zbus_chan_name(chan));
	}

	/* Read channel data */
	char payload[256];
	int payload_len;
	size_t msg_size = zbus_chan_msg_size(chan);

	/* Handle different data types */
	if (msg_size == sizeof(float)) {
		float value;
		if (zbus_chan_read(chan, &value, K_MSEC(100)) == 0) {
			payload_len = snprintf(payload, sizeof(payload), "%.2f", value);
		} else {
			return;
		}
	} else if (msg_size == sizeof(int32_t)) {
		int32_t value;
		if (zbus_chan_read(chan, &value, K_MSEC(100)) == 0) {
			payload_len = snprintf(payload, sizeof(payload), "%d", value);
		} else {
			return;
		}
	} else if (msg_size == sizeof(bool)) {
		bool value;
		if (zbus_chan_read(chan, &value, K_MSEC(100)) == 0) {
			payload_len =
				snprintf(payload, sizeof(payload), "%s", value ? "ON" : "OFF");
		} else {
			return;
		}
	} else {
		/* For complex types, read as raw data and send as hex or skip */
		LOG_WRN("Complex data type size %u not supported for auto-formatting", msg_size);
		return;
	}

	/* Publish to MQTT */
	struct mqtt_publish_param param = {
		.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.message.topic.topic.utf8 = (uint8_t *)topic,
		.message.topic.topic.size = strlen(topic),
		.message.payload.data = payload,
		.message.payload.len = payload_len,
		.message_id = sys_rand32_get(),
		.dup_flag = 0,
		.retain_flag = 0,
	};

	LOG_DBG("Publishing to %s: %s", topic, payload);

	int ret = mqtt_publish(&client, &param);
	if (ret) {
		LOG_ERR("Failed to publish: %d", ret);
	}
}

/* Main Home Assistant thread */
static void homeassistant_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	int ret;

	LOG_INF("Home Assistant integration starting...");

	/* Initialize MQTT client */
	ret = ha_mqtt_client_init();
	if (ret) {
		LOG_ERR("Failed to initialize MQTT client: %d", ret);
		return;
	}

	while (1) {
		/* Connect to broker */
		ret = ha_mqtt_connect();
		if (ret) {
			LOG_WRN("Connection failed, retrying in 5s...");
			k_sleep(K_SECONDS(5));
			continue;
		}

		/* Keep connection alive and process events */
		while (mqtt_connected) {
			ret = zsock_poll(fds, 1, K_SECONDS(60).ticks);

			if (ret < 0) {
				LOG_ERR("Poll error: %d", errno);
				break;
			}

			if (ret == 0) {
				/* Timeout - send keep alive */
				mqtt_live(&client);
				continue;
			}

			if (fds[0].revents & ZSOCK_POLLIN) {
				mqtt_input(&client);
			}

			if (fds[0].revents & (ZSOCK_POLLERR | ZSOCK_POLLHUP | ZSOCK_POLLNVAL)) {
				LOG_ERR("Socket error");
				break;
			}
		}

		/* Disconnect and retry */
		(void)mqtt_disconnect(&client, 0);
		k_sleep(K_SECONDS(5));
	}
}

K_THREAD_DEFINE(homeassistant_tid, CONFIG_HOMEASSISTANT_STACK_SIZE, homeassistant_thread, NULL,
		NULL, NULL, CONFIG_HOMEASSISTANT_THREAD_PRIORITY, 0, 0);

/* Public API */

int homeassistant_register_channel(const struct homeassistant_channel_config *config)
{
	if (!config || !config->channel) {
		return -EINVAL;
	}

	k_mutex_lock(&registry_mutex, K_FOREVER);

	/* Find free slot */
	int free_idx = -1;
	for (int i = 0; i < CONFIG_HOMEASSISTANT_MAX_CHANNELS; i++) {
		if (!channel_registry[i].in_use) {
			free_idx = i;
			break;
		}
	}

	if (free_idx < 0) {
		k_mutex_unlock(&registry_mutex);
		LOG_ERR("No free channel slots");
		return -ENOMEM;
	}

	/* Register the global listener to this channel */
	int ret = zbus_chan_add_obs(config->channel, &ha_listener, K_FOREVER);
	if (ret) {
		k_mutex_unlock(&registry_mutex);
		LOG_ERR("Failed to add observer: %d", ret);
		return ret;
	}

	channel_registry[free_idx].config = config;
	channel_registry[free_idx].in_use = true;

	k_mutex_unlock(&registry_mutex);

	LOG_INF("Registered channel: %s", zbus_chan_name(config->channel));

	/* If already connected, publish discovery */
	if (mqtt_connected) {
		publish_discovery(config);
	}

	return 0;
}

int homeassistant_unregister_channel(const struct zbus_channel *channel)
{
	if (!channel) {
		return -EINVAL;
	}

	k_mutex_lock(&registry_mutex, K_FOREVER);

	for (int i = 0; i < CONFIG_HOMEASSISTANT_MAX_CHANNELS; i++) {
		if (channel_registry[i].in_use && channel_registry[i].config->channel == channel) {

			zbus_chan_rm_obs(channel, &ha_listener, K_FOREVER);
			channel_registry[i].in_use = false;
			channel_registry[i].config = NULL;

			k_mutex_unlock(&registry_mutex);
			LOG_INF("Unregistered channel: %s", zbus_chan_name(channel));
			return 0;
		}
	}

	k_mutex_unlock(&registry_mutex);
	return -ENOENT;
}

bool homeassistant_is_connected(void)
{
	return mqtt_connected;
}

int homeassistant_publish(const char *topic, const void *payload, size_t payload_len)
{
	if (!topic || !payload) {
		return -EINVAL;
	}

	if (!mqtt_connected) {
		return -ENOTCONN;
	}

	struct mqtt_publish_param param = {
		.message.topic.qos = MQTT_QOS_0_AT_MOST_ONCE,
		.message.topic.topic.utf8 = (uint8_t *)topic,
		.message.topic.topic.size = strlen(topic),
		.message.payload.data = (uint8_t *)payload,
		.message.payload.len = payload_len,
		.message_id = sys_rand32_get(),
		.dup_flag = 0,
		.retain_flag = 0,
	};

	int ret = mqtt_publish(&client, &param);
	if (ret) {
		LOG_ERR("Failed to publish: %d", ret);
		return ret;
	}

	return 0;
}
