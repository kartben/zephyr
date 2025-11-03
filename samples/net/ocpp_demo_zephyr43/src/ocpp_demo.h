/*
 * Copyright (c) 2025 Linumiz GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef OCPP_DEMO_H_
#define OCPP_DEMO_H_

#include <zephyr/kernel.h>
#include <zephyr/net/ocpp.h>

#define NO_OF_CONN 2

/* Connector states */
enum connector_state {
	CONN_STATE_AVAILABLE = 0,
	CONN_STATE_PREPARING,
	CONN_STATE_CHARGING,
	CONN_STATE_FINISHING,
	CONN_STATE_FAULTED
};

/* System status information */
struct system_status {
	bool network_connected;
	bool ocpp_connected;
	bool usb_connected;
	uint32_t uptime_seconds;
	uint8_t cpu_load_percent;
	uint32_t cpu_freq_mhz;
	char ip_address[16];
};

/* Connector information */
struct connector_info {
	int id;
	enum connector_state state;
	char idtag[25];
	uint32_t meter_value_wh;
	uint32_t transaction_id;
	bool authorized;
};

/* Global state */
extern struct system_status g_system_status;
extern struct connector_info g_connectors[NO_OF_CONN];
extern struct k_mutex g_state_mutex;

/* GUI functions */
void gui_init(void);
void gui_update_system_status(void);
void gui_update_connector(int connector_id);
void gui_update_all(void);

/* CPU monitor functions */
void cpu_monitor_init(void);
uint8_t cpu_monitor_get_load(void);
uint32_t cpu_monitor_get_freq(void);

/* USB status functions */
void usb_status_init(void);
bool usb_status_is_connected(void);

#endif /* OCPP_DEMO_H_ */
