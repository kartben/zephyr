/*
 * Copyright (c) 2025 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>
#include <zephyr/zbus/zbus.h>
#include <zephyr/mgmt/homeassistant/homeassistant.h>
#include <stdio.h>
#include <string.h>

LOG_MODULE_REGISTER(homeassistant, CONFIG_HOMEASSISTANT_LOG_LEVEL);

#define MAX_ENTITIES 16
#define HTTP_RECV_BUF_SIZE 512
#define HTTP_SEND_BUF_SIZE 1024

static struct homeassistant_entity_config const *registered_entities[MAX_ENTITIES];
static int entity_count;
static K_MUTEX_DEFINE(entity_mutex);

static uint8_t http_recv_buf[HTTP_RECV_BUF_SIZE];

/* Thread for monitoring Zbus channels and updating Home Assistant */
static void homeassistant_thread(void *arg1, void *arg2, void *arg3);

K_THREAD_DEFINE(homeassistant_tid, 2048, homeassistant_thread,
		NULL, NULL, NULL, 5, 0, 0);

/**
 * @brief Format entity state as JSON payload
 */
static int format_entity_state(const struct homeassistant_entity_config *entity,
				char *buf, size_t buf_size)
{
	const void *msg;
	int ret;

	if (entity == NULL || entity->channel == NULL) {
		return -EINVAL;
	}

	msg = zbus_chan_const_msg(entity->channel);
	if (msg == NULL) {
		return -ENODATA;
	}

	/* For simplicity, assume the channel message is a numeric type or string
	 * In a real implementation, you'd need type detection or user callbacks
	 */
	switch (entity->type) {
	case HOMEASSISTANT_ENTITY_SENSOR:
	case HOMEASSISTANT_ENTITY_NUMBER: {
		/* Assume the message is a simple integer or float */
		int value = *(int *)msg;

		ret = snprintf(buf, buf_size,
			       "{\"state\":%d,\"attributes\":{\"unit_of_measurement\":\"%s\"}}",
			       value, entity->unit ? entity->unit : "");
		break;
	}
	case HOMEASSISTANT_ENTITY_BINARY_SENSOR:
	case HOMEASSISTANT_ENTITY_SWITCH: {
		/* Assume the message is a boolean (int 0 or 1) */
		int value = *(int *)msg;

		ret = snprintf(buf, buf_size, "{\"state\":\"%s\"}",
			       value ? "ON" : "OFF");
		break;
	}
	default:
		return -ENOTSUP;
	}

	if (ret < 0 || ret >= buf_size) {
		return -ENOMEM;
	}

	return ret;
}

/**
 * @brief Send state update to Home Assistant
 */
static int send_state_update(const struct homeassistant_entity_config *entity)
{
	char payload[HTTP_SEND_BUF_SIZE];
	char url[128];
	int sock;
	struct sockaddr_in addr;
	int ret;

	ret = format_entity_state(entity, payload, sizeof(payload));
	if (ret < 0) {
		LOG_ERR("Failed to format entity state: %d", ret);
		return ret;
	}

	LOG_DBG("Payload: %s", payload);

	/* Create socket */
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Failed to create socket: %d", errno);
		return -errno;
	}

	/* Setup address */
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(CONFIG_HOMEASSISTANT_SERVER_PORT);
	inet_pton(AF_INET, CONFIG_HOMEASSISTANT_SERVER_ADDR, &addr.sin_addr);

	/* Connect to Home Assistant */
	ret = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("Failed to connect to Home Assistant: %d", errno);
		close(sock);
		return -errno;
	}

	/* Build URL for entity state update */
	snprintf(url, sizeof(url), "/api/states/sensor.%s", entity->name);

	/* Prepare HTTP request */
	struct http_request req;

	memset(&req, 0, sizeof(req));
	req.method = HTTP_POST;
	req.url = url;
	req.host = CONFIG_HOMEASSISTANT_SERVER_ADDR;
	req.protocol = "HTTP/1.1";
	req.payload = payload;
	req.payload_len = strlen(payload);
	req.recv_buf = http_recv_buf;
	req.recv_buf_len = sizeof(http_recv_buf);

	/* Add headers */
	static const char *headers[] = {
		"Authorization: Bearer " CONFIG_HOMEASSISTANT_API_TOKEN "\r\n",
		"Content-Type: application/json\r\n",
		NULL
	};
	req.header_fields = headers;

	/* Send request */
	ret = http_client_req(sock, &req, CONFIG_HOMEASSISTANT_UPDATE_INTERVAL, NULL);

	close(sock);

	if (ret < 0) {
		LOG_ERR("HTTP request failed: %d", ret);
		return ret;
	}

	LOG_DBG("State update sent for %s", entity->name);
	return 0;
}

/**
 * @brief Main thread for Home Assistant integration
 */
static void homeassistant_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	LOG_INF("Home Assistant integration started");
	LOG_INF("Server: %s:%d", CONFIG_HOMEASSISTANT_SERVER_ADDR,
		CONFIG_HOMEASSISTANT_SERVER_PORT);

	/* Wait a bit for network to be ready */
	k_sleep(K_SECONDS(2));

	while (1) {
		k_mutex_lock(&entity_mutex, K_FOREVER);

		/* Update all registered entities */
		for (int i = 0; i < entity_count; i++) {
			if (registered_entities[i] != NULL) {
				send_state_update(registered_entities[i]);
			}
		}

		k_mutex_unlock(&entity_mutex);

		/* Wait before next update cycle */
		k_sleep(K_MSEC(CONFIG_HOMEASSISTANT_UPDATE_INTERVAL));
	}
}

int homeassistant_register_entity(const struct homeassistant_entity_config *config)
{
	if (config == NULL || config->channel == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&entity_mutex, K_FOREVER);

	if (entity_count >= MAX_ENTITIES) {
		k_mutex_unlock(&entity_mutex);
		LOG_ERR("Maximum number of entities reached");
		return -ENOMEM;
	}

	registered_entities[entity_count++] = config;

	k_mutex_unlock(&entity_mutex);

	LOG_INF("Registered entity: %s (type=%d)", config->name, config->type);
	return 0;
}

int homeassistant_init(void)
{
	LOG_INF("Home Assistant integration initialized");
	return 0;
}

SYS_INIT(homeassistant_init, APPLICATION, 90);
