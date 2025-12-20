/*
 * Copyright (c) 2022 G-Technologies Sdn. Bhd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Shell MQTT transport backend.
 */

#ifndef ZEPHYR_INCLUDE_SHELL_MQTT_H_
#define ZEPHYR_INCLUDE_SHELL_MQTT_H_

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/sys/ring_buffer.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup shell_api
 * @{
 */

/**
 * @cond INTERNAL_HIDDEN
 */

#define RX_RB_SIZE CONFIG_SHELL_MQTT_RX_BUF_SIZE
#define TX_BUF_SIZE CONFIG_SHELL_MQTT_TX_BUF_SIZE
#define SH_MQTT_BUFFER_SIZE 64
#define DEVICE_ID_BIN_MAX_SIZE 3
#define DEVICE_ID_HEX_MAX_SIZE ((DEVICE_ID_BIN_MAX_SIZE * 2) + 1)
#define SH_MQTT_TOPIC_RX_MAX_SIZE DEVICE_ID_HEX_MAX_SIZE + sizeof(CONFIG_SHELL_MQTT_TOPIC_RX_ID)
#define SH_MQTT_TOPIC_TX_MAX_SIZE DEVICE_ID_HEX_MAX_SIZE + sizeof(CONFIG_SHELL_MQTT_TOPIC_TX_ID)

extern const struct shell_transport_api shell_mqtt_transport_api;

/**
 * @brief Shell MQTT TX buffer.
 */
struct shell_mqtt_tx_buf {
	char buf[TX_BUF_SIZE]; /**< TX buffer. */
	uint16_t len;          /**< Current TX buffer length. */
};

/**
 * @brief MQTT-based shell transport context.
 */
struct shell_mqtt {
	char device_id[DEVICE_ID_HEX_MAX_SIZE];        /**< Device ID. */
	char sub_topic[SH_MQTT_TOPIC_RX_MAX_SIZE];     /**< Subscribe topic. */
	char pub_topic[SH_MQTT_TOPIC_TX_MAX_SIZE];     /**< Publish topic. */
	shell_transport_handler_t shell_handler;       /**< Handler registered by shell. */
	struct ring_buf rx_rb;                         /**< RX ring buffer. */
	uint8_t rx_rb_buf[RX_RB_SIZE];                 /**< RX ring buffer storage. */
	uint8_t *rx_rb_ptr;                            /**< RX ring buffer pointer. */
	struct shell_mqtt_tx_buf tx_buf;               /**< TX buffer. */
	void *shell_context;                           /**< Context registered by shell. */
	struct mqtt_client mqtt_cli;                   /**< MQTT client. */

	/** Buffers for MQTT client. */
	struct buffer {
		uint8_t rx[SH_MQTT_BUFFER_SIZE];       /**< RX buffer. */
		uint8_t tx[SH_MQTT_BUFFER_SIZE];       /**< TX buffer. */
	} buf;

	struct k_mutex lock;                           /**< Mutex lock. */
	struct net_sockaddr_storage broker;            /**< MQTT broker details. */
	struct zsock_addrinfo *haddr;                  /**< Host address info. */
	struct zsock_pollfd fds[1];                    /**< Poll file descriptors. */
	int nfds;                                      /**< Number of file descriptors. */
	struct mqtt_publish_param pub_data;            /**< Publish parameters. */
	struct net_mgmt_event_callback mgmt_cb;        /**< Network management callback. */

	/** Work queue and work items. */
	struct k_work_q workq;                         /**< Work queue. */
	struct k_work net_disconnected_work;           /**< Network disconnected work. */
	struct k_work_delayable connect_dwork;         /**< Connect work. */
	struct k_work_delayable subscribe_dwork;       /**< Subscribe work. */
	struct k_work_delayable process_dwork;         /**< Process work. */
	struct k_work_delayable publish_dwork;         /**< Publish work. */

	/** MQTT connection states. */
	enum sh_mqtt_transport_state {
		SHELL_MQTT_TRANSPORT_DISCONNECTED, /**< Disconnected. */
		SHELL_MQTT_TRANSPORT_CONNECTED,    /**< Connected. */
	} transport_state;

	/** MQTT subscription states. */
	enum sh_mqtt_subscribe_state {
		SHELL_MQTT_NOT_SUBSCRIBED, /**< Not subscribed. */
		SHELL_MQTT_SUBSCRIBED,     /**< Subscribed. */
	} subscribe_state;

	/** Network states. */
	enum sh_mqtt_network_state {
		SHELL_MQTT_NETWORK_DISCONNECTED, /**< Network disconnected. */
		SHELL_MQTT_NETWORK_CONNECTED,    /**< Network connected. */
	} network_state;
};

/**
 * @endcond
 */

/**
 * @brief Macro for creating shell MQTT transport instance.
 *
 * @param _name Instance name.
 */
#define SHELL_MQTT_DEFINE(_name)                                                                   \
	static struct shell_mqtt _name##_shell_mqtt;                                               \
	struct shell_transport _name = { .api = &shell_mqtt_transport_api,                         \
					 .ctx = (struct shell_mqtt *)&_name##_shell_mqtt }

/**
 * @brief Get pointer to the shell MQTT backend instance.
 *
 * This function returns a pointer to the shell MQTT instance. This instance
 * can be used with shell_execute_cmd() to test command behavior.
 *
 * @return Pointer to the shell instance.
 */
const struct shell *shell_backend_mqtt_get_ptr(void);

/**
 * @brief Get device ID for MQTT shell backend.
 *
 * This function defines the device ID (devid) used by the shell MQTT backend
 * as a client ID when connecting to the broker. The shell publishes output to
 * devid_tx and subscribes to devid_rx for input.
 *
 * @note This is a weak-linked function and can be overridden if desired.
 *
 * @param id         Pointer to the devid buffer.
 * @param id_max_len Maximum size of the devid buffer (DEVICE_ID_HEX_MAX_SIZE).
 *
 * @retval true  If length of devid > 0.
 * @retval false If devid is empty.
 */
bool shell_mqtt_get_devid(char *id, int id_max_len);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_SHELL_MQTT_H_ */
