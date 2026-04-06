/*
 * Copyright (c) 2025 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/net_event.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi_p2p, LOG_LEVEL_INF);

#define WIFI_P2P_MGMT_EVENTS                                                                       \
	(NET_EVENT_WIFI_P2P_DEV_FOUND | NET_EVENT_WIFI_P2P_CONNECTION_REQUESTED |                  \
	 NET_EVENT_WIFI_P2P_GO_NEG_REQ | NET_EVENT_WIFI_P2P_GROUP_STARTED |                        \
	 NET_EVENT_WIFI_P2P_GROUP_REMOVED)

static struct net_mgmt_event_callback p2p_cb;

static void handle_p2p_event(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			      struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_P2P_DEV_FOUND: {
		const struct wifi_p2p_device_info *info =
			(const struct wifi_p2p_device_info *)cb->info;

		LOG_INF("P2P peer found: %s (%02x:%02x:%02x:%02x:%02x:%02x)",
			info->device_name, info->mac_addr[0], info->mac_addr[1],
			info->mac_addr[2], info->mac_addr[3], info->mac_addr[4],
			info->mac_addr[5]);
		break;
	}

	case NET_EVENT_WIFI_P2P_CONNECTION_REQUESTED:
		LOG_INF("P2P connection requested");
		break;

	case NET_EVENT_WIFI_P2P_GO_NEG_REQ:
		LOG_INF("P2P GO negotiation request received");
		break;

	case NET_EVENT_WIFI_P2P_GROUP_STARTED:
		LOG_INF("P2P group started");
		break;

	case NET_EVENT_WIFI_P2P_GROUP_REMOVED:
		LOG_INF("P2P group removed");
		break;

	default:
		break;
	}
}

int main(void)
{
	LOG_INF("Wi-Fi P2P sample started");
	LOG_INF("Use the Wi-Fi shell ('wifi p2p') to control P2P discovery and connections.");

	net_mgmt_init_event_callback(&p2p_cb, handle_p2p_event, WIFI_P2P_MGMT_EVENTS);
	net_mgmt_add_event_callback(&p2p_cb);

	return 0;
}
