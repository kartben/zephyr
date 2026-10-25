/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/cleanup.h>
#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/util.h>

#define FINGERPRINT_NODE DT_ALIAS(fingerprint)

#if !DT_NODE_EXISTS(FINGERPRINT_NODE)
#error "Fingerprint sensor not defined. Add 'fingerprint = &fingerprint;' to your DTS aliases"
#endif

#if DT_NODE_HAS_COMPAT(FINGERPRINT_NODE, zephyr_biometrics_emul)
#include <zephyr/drivers/biometrics/emul.h>
#endif

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#define MACARG(_mac) (_mac)[0], (_mac)[1], (_mac)[2], (_mac)[3], (_mac)[4], (_mac)[5]
static const struct device *const fingerprint = DEVICE_DT_GET(FINGERPRINT_NODE);

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P
#define WIFI_DIRECT_EVENT_MASK                                                                  \
	(NET_EVENT_WIFI_P2P_DEVICE_FOUND | NET_EVENT_WIFI_CONNECT_RESULT)

struct wifi_direct_demo_state {
	struct net_if *iface;
	uint8_t peer_mac[WIFI_MAC_ADDR_LEN];
	struct wifi_status connect_status;
};

struct p2p_discovery_scope {
	struct net_if *iface;
	bool started;
};

static struct wifi_direct_demo_state demo_state;
static K_SEM_DEFINE(peer_found_sem, 0, 1);
static K_SEM_DEFINE(connect_done_sem, 0, 1);

static int stop_p2p_find(struct net_if *iface)
{
	struct wifi_p2p_params params = {
		.oper = WIFI_P2P_STOP_FIND,
	};

	return net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));
}

SCOPE_VAR_DEFINE(net_mgmt_cb, struct net_mgmt_event_callback *,
		 if (_T != NULL) {
			 net_mgmt_del_event_callback(_T);
		 },
		 cb_ptr,
		 struct net_mgmt_event_callback *cb_ptr);

SCOPE_VAR_DEFINE(p2p_discovery, struct p2p_discovery_scope,
		 if (_T.started && _T.iface != NULL) {
			 (void)stop_p2p_find(_T.iface);
		 },
		 ((struct p2p_discovery_scope){ .iface = iface, .started = false }),
		 struct net_if *iface);

static int parse_peer_mac(uint8_t mac[WIFI_MAC_ADDR_LEN])
{
	if (sscanf(CONFIG_FINGERPRINT_WIFI_DIRECT_PEER_MAC,
		   "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
		   &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]) !=
	    WIFI_MAC_ADDR_LEN) {
		return -EINVAL;
	}

	return 0;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	if (iface != demo_state.iface || cb->info == NULL) {
		return;
	}

	switch (mgmt_event) {
	case NET_EVENT_WIFI_P2P_DEVICE_FOUND: {
		const struct wifi_p2p_device_info *peer = cb->info;

		if (memcmp(peer->mac, demo_state.peer_mac, WIFI_MAC_ADDR_LEN) != 0) {
			return;
		}

		if (peer->device_name[0] != '\0') {
			LOG_INF("Matched peer " MACSTR " (%s)", MACARG(peer->mac),
				peer->device_name);
		} else {
			LOG_INF("Matched peer " MACSTR, MACARG(peer->mac));
		}
		k_sem_give(&peer_found_sem);
		break;
	}

	case NET_EVENT_WIFI_CONNECT_RESULT: {
		const struct wifi_status *status = cb->info;

		demo_state.connect_status = *status;
		k_sem_give(&connect_done_sem);
		break;
	}

	default:
		break;
	}
}
#endif

static int wait_for_valid_user(struct biometric_match_result *result)
{
	int ret;

	LOG_INF("Waiting for an enrolled user...");

	while (true) {
		ret = biometric_match(fingerprint, BIOMETRIC_MATCH_IDENTIFY, 0,
				      K_SECONDS(CONFIG_FINGERPRINT_WIFI_DIRECT_SCAN_TIMEOUT_SEC),
				      result);
		if (ret == 0) {
			LOG_INF("User %u authenticated (confidence %d, quality %u)",
				result->template_id, result->confidence, result->image_quality);
			return 0;
		}

		if (ret == -ENOENT) {
			LOG_WRN("No valid user found, retrying...");
		} else if (ret == -ETIMEDOUT) {
			LOG_WRN("Biometric scan timed out, retrying...");
		} else {
			return ret;
		}

		k_sleep(K_MSEC(250));
	}
}

static int connect_to_peer(struct net_if *iface)
{
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P
	struct net_mgmt_event_callback cb;
	struct wifi_p2p_params find_params = {
		.oper = WIFI_P2P_FIND,
		.discovery_type = WIFI_P2P_FIND_START_WITH_FULL,
		.timeout = CONFIG_FINGERPRINT_WIFI_DIRECT_DISCOVERY_TIMEOUT_SEC,
	};
	struct wifi_p2p_params connect_params = {
		.oper = WIFI_P2P_CONNECT,
		.connect.method = WIFI_P2P_METHOD_PBC,
		.connect.go_intent = CONFIG_FINGERPRINT_WIFI_DIRECT_GO_INTENT,
		.connect.freq = CONFIG_FINGERPRINT_WIFI_DIRECT_FREQUENCY,
		.connect.join = IS_ENABLED(CONFIG_FINGERPRINT_WIFI_DIRECT_JOIN),
	};
	int ret;

	net_mgmt_init_event_callback(&cb, wifi_event_handler, WIFI_DIRECT_EVENT_MASK);
	net_mgmt_add_event_callback(&cb);
	scope_var(net_mgmt_cb, callback_cleanup)(&cb);
	scope_var(p2p_discovery, discovery)(iface);

	if (callback_cleanup == NULL) {
		return -EINVAL;
	}

	demo_state.iface = iface;
	k_sem_reset(&peer_found_sem);
	k_sem_reset(&connect_done_sem);

	ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &find_params, sizeof(find_params));
	if (ret < 0) {
		return ret;
	}

	discovery.started = true;

	LOG_INF("Searching for Wi-Fi Direct peer " MACSTR, MACARG(demo_state.peer_mac));

	ret = k_sem_take(&peer_found_sem,
			 K_SECONDS(CONFIG_FINGERPRINT_WIFI_DIRECT_DISCOVERY_TIMEOUT_SEC));
	if (ret < 0) {
		return ret == -EAGAIN ? -ETIMEDOUT : ret;
	}

	ret = stop_p2p_find(iface);
	discovery.started = false;
	if (ret < 0 && ret != -EALREADY) {
		return ret;
	}

	memcpy(connect_params.peer_addr, demo_state.peer_mac, sizeof(connect_params.peer_addr));

	ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &connect_params, sizeof(connect_params));
	if (ret < 0) {
		return ret;
	}

	ret = k_sem_take(&connect_done_sem,
			 K_SECONDS(CONFIG_FINGERPRINT_WIFI_DIRECT_CONNECT_TIMEOUT_SEC));
	if (ret < 0) {
		return ret == -EAGAIN ? -ETIMEDOUT : ret;
	}

	if (demo_state.connect_status.status != 0) {
		LOG_ERR("Wi-Fi Direct connection failed (%d)", demo_state.connect_status.status);
		return -EIO;
	}

	LOG_INF("Wi-Fi Direct connection established");

	return 0;
#else
	ARG_UNUSED(iface);

	LOG_INF("Rebuild with p2p.conf on supported hardware to enable Wi-Fi Direct");

	return -ENOTSUP;
#endif
}

static int prepare_demo_user(void)
{
#if DT_NODE_HAS_COMPAT(FINGERPRINT_NODE, zephyr_biometrics_emul)
	static const uint8_t template_data[64] = {
		1, 0, 0xaa, 0x55,
	};
	int ret;

	ret = biometric_template_store(fingerprint, 1, template_data, sizeof(template_data));
	if (ret < 0 && ret != -EEXIST) {
		return ret;
	}

	biometrics_emul_set_match_id(fingerprint, 1);
	biometrics_emul_set_match_score(fingerprint, 95);
	biometrics_emul_set_image_quality(fingerprint, 90);
#endif

	return 0;
}

int main(void)
{
	struct biometric_match_result result;
	int ret;

	LOG_INF("Fingerprint Wi-Fi Direct demo");

	if (!device_is_ready(fingerprint)) {
		LOG_ERR("Fingerprint sensor is not ready");
		return -ENODEV;
	}

	ret = prepare_demo_user();
	if (ret < 0) {
		LOG_ERR("Failed to prepare demo user: %d", ret);
		return ret;
	}

	ret = wait_for_valid_user(&result);
	if (ret < 0) {
		LOG_ERR("Biometric identification failed: %d", ret);
		return ret;
	}

#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P
	{
		struct net_if *iface = net_if_get_wifi_sta();

		ret = parse_peer_mac(demo_state.peer_mac);
		if (ret < 0) {
			LOG_ERR("Invalid peer MAC address: %s",
				CONFIG_FINGERPRINT_WIFI_DIRECT_PEER_MAC);
			return ret;
		}

		if (iface == NULL) {
			LOG_ERR("No Wi-Fi station interface available for Wi-Fi Direct");
			return -ENODEV;
		}

		ret = connect_to_peer(iface);
		if (ret == -ETIMEDOUT) {
			LOG_ERR("Timed out waiting for peer " MACSTR, MACARG(demo_state.peer_mac));
		} else if (ret < 0) {
			LOG_ERR("Failed to connect to Wi-Fi Direct peer: %d", ret);
		}

		return ret;
	}
#else
	ret = connect_to_peer(NULL);
	return (ret == -ENOTSUP) ? 0 : ret;
#endif
}
