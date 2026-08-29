/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_BIOMETRICS_EMUL
#include <zephyr/drivers/biometrics/emul.h>
#endif

LOG_MODULE_REGISTER(wifi_direct_biometrics, LOG_LEVEL_INF);

#define LED0_NODE DT_ALIAS(led0)
#define BIOMETRICS_NODE DT_ALIAS(biometrics)

#define AUTH_TEMPLATE_ID 1
#define AUTH_BLINK_INTERVAL_MS 150
#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define WIFI_EVENT_MASK                                                                            \
	(NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |                      \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED)

#if !DT_NODE_EXISTS(LED0_NODE)
#error "This sample requires a led0 devicetree alias"
#endif

#if !DT_NODE_EXISTS(BIOMETRICS_NODE)
#error "This sample requires a biometrics devicetree alias"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);
static const struct device *const biometrics = DEVICE_DT_GET(BIOMETRICS_NODE);

static struct net_mgmt_event_callback wifi_cb;
static struct net_if *wifi_iface;
static struct net_if *pending_peer_iface;
static uint8_t pending_peer_mac[WIFI_MAC_ADDR_LEN];
static atomic_t auth_busy;

K_SEM_DEFINE(auth_sem, 0, 1);

static void disconnect_peer(struct net_if *iface, const uint8_t *mac)
{
	int ret;

	if (iface == NULL) {
		return;
	}

	ret = net_mgmt(NET_REQUEST_WIFI_AP_STA_DISCONNECT, iface, (void *)mac, WIFI_MAC_ADDR_LEN);
	if (ret != 0) {
		LOG_WRN("Failed to disconnect peer " MACSTR " (%d)", mac[0], mac[1], mac[2], mac[3],
			mac[4], mac[5], ret);
	}
}

static void configure_led(void)
{
	if (!gpio_is_ready_dt(&led)) {
		LOG_WRN("led0 is not ready");
		return;
	}

	if (gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE) != 0) {
		LOG_WRN("Unable to configure led0");
	}
}

static void set_led(bool on)
{
	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	(void)gpio_pin_set_dt(&led, on);
}

static int seed_demo_template(void)
{
#ifdef CONFIG_BIOMETRICS_EMUL
	static const uint8_t template_data[64] = {
		0x42,
	};
	int ret;

	ret = biometric_template_delete_all(biometrics);
	if (ret < 0) {
		return ret;
	}

	ret = biometric_template_store(biometrics, AUTH_TEMPLATE_ID, template_data,
				       sizeof(template_data));
	if (ret < 0) {
		return ret;
	}

	biometrics_emul_set_match_id(biometrics, AUTH_TEMPLATE_ID);
	biometrics_emul_set_match_score(biometrics, 90);
	biometrics_emul_set_image_quality(biometrics, 95);
	biometrics_emul_set_match_fail(biometrics, false);

	LOG_INF("Seeded demo biometric template %d", AUTH_TEMPLATE_ID);
#endif

	return 0;
}

static int authenticate_peer(const uint8_t *mac)
{
	struct biometric_match_result result = {0};
	int ret;

	LOG_INF("Peer " MACSTR " requested access", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
	LOG_INF("Blinking led0 while waiting for biometric approval");

	ret = biometric_match(biometrics, BIOMETRIC_MATCH_VERIFY, AUTH_TEMPLATE_ID, K_SECONDS(5),
			      &result);

	if (ret == 0) {
		LOG_INF("Peer " MACSTR " authenticated (confidence %d, quality %u)", mac[0], mac[1],
			mac[2], mac[3], mac[4], mac[5], result.confidence, result.image_quality);
		return 0;
	}

	if (ret == -ENOENT) {
		LOG_WRN("Peer " MACSTR " rejected by biometrics", mac[0], mac[1], mac[2], mac[3],
			mac[4], mac[5]);
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Biometric authentication timed out for peer " MACSTR, mac[0], mac[1], mac[2],
			mac[3], mac[4], mac[5]);
	} else {
		LOG_ERR("Biometric authentication failed for peer " MACSTR " (%d)", mac[0], mac[1],
			mac[2], mac[3], mac[4], mac[5], ret);
	}

	return ret;
}

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		LOG_INF("Wi-Fi Direct hotspot enabled, waiting for peers");
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		LOG_INF("Wi-Fi Direct hotspot disabled");
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
		const struct wifi_ap_sta_info *sta_info = cb->info;

		if (!atomic_cas(&auth_busy, 0, 1)) {
			LOG_WRN("Already authenticating a peer, rejecting " MACSTR, sta_info->mac[0],
				sta_info->mac[1], sta_info->mac[2], sta_info->mac[3], sta_info->mac[4],
				sta_info->mac[5]);
			disconnect_peer(iface, sta_info->mac);
			break;
		}

		memcpy(pending_peer_mac, sta_info->mac, sizeof(pending_peer_mac));
		pending_peer_iface = iface;
		k_sem_give(&auth_sem);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
		const struct wifi_ap_sta_info *sta_info = cb->info;

		LOG_INF("Peer " MACSTR " disconnected", sta_info->mac[0], sta_info->mac[1],
			sta_info->mac[2], sta_info->mac[3], sta_info->mac[4], sta_info->mac[5]);
		break;
	}
	default:
		break;
	}
}

static void auth_thread(void *arg1, void *arg2, void *arg3)
{
	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		k_sem_take(&auth_sem, K_FOREVER);

		if (authenticate_peer(pending_peer_mac) != 0) {
			disconnect_peer(pending_peer_iface, pending_peer_mac);
		}

		set_led(false);
		atomic_clear(&auth_busy);
	}
}

static void blink_thread(void *arg1, void *arg2, void *arg3)
{
	bool led_on = false;

	ARG_UNUSED(arg1);
	ARG_UNUSED(arg2);
	ARG_UNUSED(arg3);

	while (true) {
		if (atomic_get(&auth_busy) != 0) {
			led_on = !led_on;
			set_led(led_on);
			k_sleep(K_MSEC(AUTH_BLINK_INTERVAL_MS));
			continue;
		}

		if (led_on) {
			led_on = false;
			set_led(false);
		}

		k_sleep(K_MSEC(AUTH_BLINK_INTERVAL_MS));
	}
}

K_THREAD_DEFINE(auth_tid, 2048, auth_thread, NULL, NULL, NULL, 5, 0, 0);
K_THREAD_DEFINE(blink_tid, 1024, blink_thread, NULL, NULL, NULL, 6, 0, 0);

static int start_wifi_direct_hotspot(void)
{
#ifdef CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P
	struct wifi_p2p_params params = {
		.oper = WIFI_P2P_GROUP_ADD,
		.group_add = {
			.persistent = -1,
		},
	};
	int ret;

	ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, wifi_iface, &params, sizeof(params));
	if (ret != 0) {
		LOG_ERR("Failed to start Wi-Fi Direct hotspot (%d)", ret);
		return ret;
	}

	LOG_INF("Requested autonomous Wi-Fi Direct group-owner hotspot");
	return 0;
#else
	LOG_ERR("Wi-Fi Direct P2P support is not enabled for this build");
	return -ENOTSUP;
#endif
}

static void simulate_peer_attempt(void)
{
	static const uint8_t simulated_peer[WIFI_MAC_ADDR_LEN] = { 0x02, 0x00, 0x00, 0x00, 0x00, 0x01 };

	LOG_WRN("No Wi-Fi interface found, simulating a peer attempt");
	atomic_set(&auth_busy, 1);
	memcpy(pending_peer_mac, simulated_peer, sizeof(pending_peer_mac));
	pending_peer_iface = NULL;
	k_sleep(K_SECONDS(1));
	k_sem_give(&auth_sem);
}

int main(void)
{
	int ret;

	configure_led();

	if (!device_is_ready(biometrics)) {
		LOG_ERR("Biometrics device is not ready");
		return -ENODEV;
	}

	ret = seed_demo_template();
	if (ret < 0) {
		LOG_ERR("Failed to prepare demo biometrics data (%d)", ret);
		return ret;
	}

	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, WIFI_EVENT_MASK);
	net_mgmt_add_event_callback(&wifi_cb);

	wifi_iface = net_if_get_wifi_sta();
	if (wifi_iface == NULL) {
		wifi_iface = net_if_get_first_wifi();
	}

	if (wifi_iface == NULL) {
		simulate_peer_attempt();
		return 0;
	}

	return start_wifi_direct_hotspot();
}
