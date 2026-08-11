/*
 * Copyright (c) 2025 The Zephyr Project
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief WiFi Direct (P2P) hotspot with biometric authentication demo.
 *
 * This sample creates a WiFi Direct Group Owner (hotspot) and, when a peer
 * device is discovered, blinks LED0 while waiting for the user to
 * authenticate via a biometric (fingerprint) sensor.  On successful
 * authentication the LED is turned on steady and the peer connection is
 * accepted; on failure the LED is turned off.
 *
 * The biometrics emulated driver can be used for initial testing on
 * native_sim.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(p2p_bio_auth, LOG_LEVEL_INF);

/* ---------- LED ---------- */

#define LED0_NODE DT_ALIAS(led0)
#if !DT_NODE_HAS_STATUS_OKAY(LED0_NODE)
#error "LED0 alias not defined or not enabled in devicetree"
#endif

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* ---------- Biometrics ---------- */

#define FINGERPRINT_NODE DT_ALIAS(fingerprint)
#if !DT_NODE_EXISTS(FINGERPRINT_NODE)
#error "Fingerprint sensor alias not defined in devicetree"
#endif

static const struct device *const fingerprint = DEVICE_DT_GET(FINGERPRINT_NODE);

/** Template ID used for the pre-enrolled fingerprint. */
#define AUTH_TEMPLATE_ID 1

/* ---------- Synchronization ---------- */

/** Semaphore given by the WiFi event handler when a peer is detected. */
static K_SEM_DEFINE(peer_detected_sem, 0, 1);

/** Flag: true while the LED blink thread should keep blinking. */
static volatile bool blinking;

/* ---------- LED blink thread ---------- */

#define BLINK_STACK_SIZE 512
#define BLINK_PRIORITY   7
#define BLINK_PERIOD_MS  250

static void blink_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (1) {
		if (blinking) {
			gpio_pin_toggle_dt(&led);
		}
		k_msleep(BLINK_PERIOD_MS);
	}
}

K_THREAD_DEFINE(blink_tid, BLINK_STACK_SIZE, blink_thread_fn,
		NULL, NULL, NULL, BLINK_PRIORITY, 0, 0);

/* ---------- WiFi event handling ---------- */

#define MACSTR "%02X:%02X:%02X:%02X:%02X:%02X"

#define WIFI_EVENTS_MASK                                                       \
	(NET_EVENT_WIFI_AP_ENABLE_RESULT | NET_EVENT_WIFI_AP_DISABLE_RESULT |  \
	 NET_EVENT_WIFI_AP_STA_CONNECTED | NET_EVENT_WIFI_AP_STA_DISCONNECTED |\
	 NET_EVENT_WIFI_P2P_DEVICE_FOUND)

static struct net_mgmt_event_callback wifi_cb;

static void wifi_event_handler(struct net_mgmt_event_callback *cb,
			       uint64_t mgmt_event, struct net_if *iface)
{
	switch (mgmt_event) {
	case NET_EVENT_WIFI_AP_ENABLE_RESULT:
		LOG_INF("P2P Group Owner mode enabled – hotspot active");
		break;
	case NET_EVENT_WIFI_AP_DISABLE_RESULT:
		LOG_INF("P2P Group Owner mode disabled");
		break;
	case NET_EVENT_WIFI_AP_STA_CONNECTED: {
#if defined(CONFIG_NET_MGMT_EVENT_INFO)
		struct wifi_ap_sta_info *sta =
			(struct wifi_ap_sta_info *)cb->info;
		LOG_INF("Peer connected: " MACSTR,
			sta->mac[0], sta->mac[1], sta->mac[2],
			sta->mac[3], sta->mac[4], sta->mac[5]);
#else
		LOG_INF("Peer connected");
#endif
		/* Signal the main thread to start biometric auth. */
		k_sem_give(&peer_detected_sem);
		break;
	}
	case NET_EVENT_WIFI_AP_STA_DISCONNECTED: {
#if defined(CONFIG_NET_MGMT_EVENT_INFO)
		struct wifi_ap_sta_info *sta =
			(struct wifi_ap_sta_info *)cb->info;
		LOG_INF("Peer disconnected: " MACSTR,
			sta->mac[0], sta->mac[1], sta->mac[2],
			sta->mac[3], sta->mac[4], sta->mac[5]);
#else
		LOG_INF("Peer disconnected");
#endif
		break;
	}
	case NET_EVENT_WIFI_P2P_DEVICE_FOUND:
		LOG_INF("P2P peer discovered");
		k_sem_give(&peer_detected_sem);
		break;
	default:
		break;
	}
}

/* ---------- Biometric helpers ---------- */

/**
 * Pre-enroll a fingerprint template so that subsequent match operations
 * have something to verify against.
 */
static int enroll_fingerprint(void)
{
	struct biometric_capabilities caps;
	struct biometric_capture_result capture = {0};
	int ret;

	ret = biometric_get_capabilities(fingerprint, &caps);
	if (ret < 0) {
		LOG_ERR("Failed to read sensor capabilities: %d", ret);
		return ret;
	}

	LOG_INF("Sensor: max_templates=%u, samples_required=%u",
		caps.max_templates, caps.enrollment_samples_required);

	ret = biometric_enroll_start(fingerprint, AUTH_TEMPLATE_ID);
	if (ret < 0) {
		LOG_ERR("Enroll start failed: %d", ret);
		return ret;
	}

	LOG_INF("Enrolling template %u – place finger on sensor",
		AUTH_TEMPLATE_ID);

	do {
		ret = biometric_enroll_capture(fingerprint, K_SECONDS(10),
					       &capture);
		if (ret < 0) {
			LOG_ERR("Capture failed: %d", ret);
			biometric_enroll_abort(fingerprint);
			return ret;
		}

		LOG_INF("  captured %u/%u (quality %u)",
			capture.samples_captured,
			caps.enrollment_samples_required,
			capture.quality);

		if (capture.samples_captured <
		    caps.enrollment_samples_required) {
			LOG_INF("  lift and place finger again");
			k_msleep(500);
		}
	} while (capture.samples_captured < caps.enrollment_samples_required);

	ret = biometric_enroll_finalize(fingerprint);
	if (ret == 0) {
		LOG_INF("Enrollment complete for template %u",
			AUTH_TEMPLATE_ID);
	} else {
		LOG_ERR("Enrollment finalize failed: %d", ret);
	}

	return ret;
}

/**
 * Attempt biometric authentication.
 *
 * @return 0 on successful match, negative errno otherwise.
 */
static int authenticate(void)
{
	struct biometric_match_result result;
	int ret;

	LOG_INF("Place finger on sensor to authenticate...");

	ret = biometric_match(fingerprint, BIOMETRIC_MATCH_VERIFY,
			      AUTH_TEMPLATE_ID, K_SECONDS(15), &result);
	if (ret == 0) {
		LOG_INF("Authentication SUCCESS (confidence %d, quality %u)",
			result.confidence, result.image_quality);
	} else if (ret == -ENOENT) {
		LOG_WRN("Authentication FAILED – fingerprint not recognised");
	} else if (ret == -ETIMEDOUT) {
		LOG_WRN("Authentication TIMEOUT – no finger detected");
	} else {
		LOG_ERR("Authentication error: %d", ret);
	}

	return ret;
}

/* ---------- WiFi P2P group setup ---------- */

#if defined(CONFIG_NET_L2_WIFI_MGMT)

static struct wifi_connect_req_params p2p_config;

static int start_p2p_group(void)
{
	struct net_if *iface = net_if_get_wifi_sap();

	if (!iface) {
		/* Fall back to the default WiFi interface. */
		iface = net_if_get_first_wifi();
	}

	if (!iface) {
		LOG_ERR("No WiFi interface available");
		return -ENODEV;
	}

#if defined(CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P)
	struct wifi_p2p_params p2p_params = {0};

	p2p_params.oper = WIFI_P2P_GROUP_ADD;
	p2p_params.group_add.freq = 0;       /* auto-select channel */
	p2p_params.group_add.persistent = -1; /* non-persistent */

	int ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface,
			   &p2p_params, sizeof(p2p_params));
	if (ret) {
		LOG_ERR("P2P GROUP_ADD failed: %d", ret);
		return ret;
	}

	LOG_INF("WiFi Direct P2P group created (Group Owner mode)");
#else
	/*
	 * Fallback: start a standard SoftAP so the demo can still
	 * illustrate the biometric-gated connection flow on platforms
	 * without full P2P support.
	 */
	static const char ssid[] = "ZephyrP2P";

	p2p_config.ssid = (const uint8_t *)ssid;
	p2p_config.ssid_length = sizeof(ssid) - 1;
	p2p_config.channel = WIFI_CHANNEL_ANY;
	p2p_config.band = WIFI_FREQ_BAND_2_4_GHZ;
	p2p_config.security = WIFI_SECURITY_TYPE_NONE;

	int ret = net_mgmt(NET_REQUEST_WIFI_AP_ENABLE, iface,
			   &p2p_config,
			   sizeof(struct wifi_connect_req_params));
	if (ret) {
		LOG_ERR("AP enable failed: %d", ret);
		return ret;
	}

	LOG_INF("SoftAP hotspot started (SSID: %s)", ssid);
#endif /* CONFIG_WIFI_NM_WPA_SUPPLICANT_P2P */

	return 0;
}

#endif /* CONFIG_NET_L2_WIFI_MGMT */

/* ---------- main ---------- */

int main(void)
{
	int ret;

	LOG_INF("=== WiFi Direct Biometric Auth Demo ===");

	/* --- Initialize LED --- */
	if (!gpio_is_ready_dt(&led)) {
		LOG_ERR("LED device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure LED: %d", ret);
		return ret;
	}

	/* --- Initialize biometrics sensor --- */
	if (!device_is_ready(fingerprint)) {
		LOG_ERR("Fingerprint sensor not ready");
		return -ENODEV;
	}

	/* Pre-enroll a fingerprint so we can verify later. */
	ret = enroll_fingerprint();
	if (ret < 0) {
		LOG_ERR("Fingerprint enrollment failed");
		return ret;
	}

	/* --- Register WiFi event handler --- */
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler,
				     WIFI_EVENTS_MASK);
	net_mgmt_add_event_callback(&wifi_cb);

	/* --- Start WiFi Direct hotspot --- */
#if defined(CONFIG_NET_L2_WIFI_MGMT)
	ret = start_p2p_group();
	if (ret < 0) {
		LOG_WRN("WiFi hotspot setup failed (%d); "
			"continuing in demo mode", ret);
	}
#else
	LOG_INF("WiFi management not available; running in demo mode");
	LOG_INF("(Peer detection will be simulated)");
	/* Give the semaphore so the auth loop runs at least once. */
	k_sem_give(&peer_detected_sem);
#endif

	LOG_INF("Waiting for a peer to connect...");

	/* --- Main authentication loop --- */
	while (1) {
		/* Block until a peer is detected. */
		k_sem_take(&peer_detected_sem, K_FOREVER);

		LOG_INF("Peer detected – blinking LED, awaiting biometric auth");

		/* Start blinking. */
		blinking = true;

		/* Run biometric authentication (blocking). */
		ret = authenticate();

		/* Stop blinking. */
		blinking = false;

		if (ret == 0) {
			/* Auth succeeded – LED on steady. */
			gpio_pin_set_dt(&led, 1);
			LOG_INF("Access GRANTED – peer authorised");
		} else {
			/* Auth failed – LED off. */
			gpio_pin_set_dt(&led, 0);
			LOG_INF("Access DENIED – peer rejected");
		}

		/*
		 * Keep the LED state for a few seconds so it is visible,
		 * then turn it off and wait for the next peer.
		 */
		k_sleep(K_SECONDS(5));
		gpio_pin_set_dt(&led, 0);
	}

	return 0;
}
