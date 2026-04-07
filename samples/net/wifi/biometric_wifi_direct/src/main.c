/*
 * Copyright (c) 2025 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Biometric + WiFi Direct demo
 * ============================
 * 1. Enroll a fingerprint on first boot (skipped if templates already exist).
 * 2. Continuously scan with BIOMETRIC_MATCH_IDENTIFY until a valid user is
 *    recognised or the retry limit is exceeded.
 * 3. On a successful match, start WiFi Direct (P2P) discovery.
 * 4. The NET_EVENT_WIFI_P2P_DEVICE_FOUND callback matches the discovered
 *    device name against CONFIG_DEMO_P2P_PEER_NAME and stores the *current*
 *    MAC address — this works correctly when the peer uses MAC randomisation.
 * 5. Connect to the resolved peer via Push-Button Configuration (PBC).
 *
 * Cleanup helpers (CONFIG_SCOPE_CLEANUP_HELPERS) are used for automatic,
 * RAII-style resource management in six places — see inline comments.
 */

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/biometrics.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/wifi.h>
#include <zephyr/cleanup/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(biometric_wifi_direct, LOG_LEVEL_INF);

/* -------------------------------------------------------------------------
 * Device tree alias → fingerprint sensor
 * ---------------------------------------------------------------------- */

#define FINGERPRINT_NODE DT_ALIAS(fingerprint)

#if !DT_NODE_EXISTS(FINGERPRINT_NODE)
#error "Add 'fingerprint = &<your-node>;' to your board's DTS aliases"
#endif

/* -------------------------------------------------------------------------
 * Custom cleanup helpers
 *
 * CLEANUP HELPER 1 — biometric_led_off
 *   Ensures the biometric sensor LED is always turned off when the enclosing
 *   scope exits, even if we return early due to an error.
 * ---------------------------------------------------------------------- */

static inline void biometric_led_off_fn(const struct device **dev)
{
	if (*dev != NULL) {
		(void)biometric_led_control(*dev, BIOMETRIC_LED_OFF);
	}
}

SCOPE_DEFER_DEFINE(biometric_led_off, const struct device **);

/* CLEANUP HELPER 2 — p2p_stop_find
 *   Stops the P2P discovery scan when p2p_connect_to_peer() returns,
 *   regardless of whether it succeeded or failed.  Prevents the radio
 *   from scanning indefinitely after the function exits.
 */

static inline void p2p_stop_find_fn(struct net_if **iface)
{
	if (*iface == NULL) {
		return;
	}

	struct wifi_p2p_params params = {.oper = WIFI_P2P_STOP_FIND};

	(void)net_mgmt(NET_REQUEST_WIFI_P2P_OPER, *iface, &params, sizeof(params));
	LOG_DBG("P2P discovery stopped (cleanup)");
}

SCOPE_DEFER_DEFINE(p2p_stop_find, struct net_if **);

/* -------------------------------------------------------------------------
 * State machine
 * ---------------------------------------------------------------------- */

enum demo_state {
	STATE_ENROLLING,
	STATE_SCANNING,
	STATE_CONNECTING,
	STATE_CONNECTED,
	STATE_ERROR,
};

static K_MUTEX_DEFINE(state_lock);
static enum demo_state g_state = STATE_ENROLLING;
static uint16_t g_matched_id;

/* -------------------------------------------------------------------------
 * Inter-thread signalling
 * ---------------------------------------------------------------------- */

/* Signalled by the scan thread when a biometric match is found (or retries
 * are exhausted).
 */
static K_SEM_DEFINE(bio_match_sem, 0, 1);

/* Signalled when the target P2P peer name is discovered and its (current)
 * MAC address has been stored in g_peer_mac.
 */
static K_SEM_DEFINE(peer_found_sem, 0, 1);

/* Signalled when a P2P connection is established (driver/supplicant event).
 * For this demo we time-out waiting for this semaphore and treat it as
 * "connection attempt delivered to supplicant".
 */
static K_SEM_DEFINE(p2p_connected_sem, 0, 1);

/* Resolved peer MAC address — written by the net_mgmt callback, read by the
 * main thread under state_lock.
 */
static uint8_t g_peer_mac[WIFI_MAC_ADDR_LEN];

/* -------------------------------------------------------------------------
 * P2P event callback — resolves peer name → MAC address
 *
 * CLEANUP HELPER 3 — scope_guard(k_mutex) inside the callback
 *   state_lock is automatically released when the guarded block exits,
 *   even if a future refactor adds an early return.
 * ---------------------------------------------------------------------- */

static struct net_mgmt_event_callback p2p_cb;

static void p2p_event_handler(struct net_mgmt_event_callback *cb, uint64_t event,
			       struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (event != NET_EVENT_WIFI_P2P_DEVICE_FOUND) {
		return;
	}

	const struct wifi_p2p_device_info *peer =
		(const struct wifi_p2p_device_info *)cb->info;

	LOG_INF("P2P device found: \"%s\" (RSSI %d dBm)", peer->device_name, peer->rssi);

	/* Name-based matching — MAC is rotated on modern devices, so we
	 * cannot rely on a static address configured at build time.
	 */
	if (strncmp(peer->device_name, CONFIG_DEMO_P2P_PEER_NAME,
		    WIFI_P2P_DEVICE_NAME_MAX_LEN) != 0) {
		return;
	}

	LOG_INF("Target peer \"%s\" found — storing resolved MAC", CONFIG_DEMO_P2P_PEER_NAME);

	/* CLEANUP HELPER 3: scope_guard keeps state_lock locked for exactly
	 * the duration of the memcpy, then releases it automatically.
	 */
	{
		scope_guard(k_mutex)(&state_lock);
		memcpy(g_peer_mac, peer->mac, WIFI_MAC_ADDR_LEN);
	}

	k_sem_give(&peer_found_sem);
}

/* -------------------------------------------------------------------------
 * Enrollment (first-boot only)
 * ---------------------------------------------------------------------- */

static int ensure_enrolled(const struct device *fp)
{
	struct biometric_capabilities caps;
	int ret;

	ret = biometric_get_capabilities(fp, &caps);
	if (ret < 0) {
		LOG_ERR("Failed to get capabilities: %d", ret);
		return ret;
	}

	LOG_INF("Sensor: max %u templates, %u samples required for enrollment",
		caps.max_templates, caps.enrollment_samples_required);

	/* Skip enrollment if at least one template already exists. */
	uint16_t ids[16];
	size_t count = 0;

	ret = biometric_template_list(fp, ids, ARRAY_SIZE(ids), &count);
	if (ret == 0 && count > 0) {
		LOG_INF("%zu template(s) already enrolled — skipping enrollment", count);
		return 0;
	}

	LOG_INF("No templates found — enrolling fingerprint (ID %d)",
		CONFIG_DEMO_BIOMETRIC_TEMPLATE_ID);

	ret = biometric_enroll_start(fp, CONFIG_DEMO_BIOMETRIC_TEMPLATE_ID);
	if (ret < 0) {
		LOG_ERR("enroll_start failed: %d", ret);
		return ret;
	}

	struct biometric_capture_result capture = {0};

	do {
		biometric_led_control(fp, BIOMETRIC_LED_BLINK);
		LOG_INF("Place finger on sensor...");

		ret = biometric_enroll_capture(fp, K_SECONDS(10), &capture);
		if (ret < 0) {
			LOG_ERR("Capture failed (%d) — aborting enrollment", ret);
			(void)biometric_enroll_abort(fp);
			(void)biometric_led_control(fp, BIOMETRIC_LED_OFF);
			return ret;
		}

		LOG_INF("Sample %u/%u captured (quality %u%%)", capture.samples_captured,
			caps.enrollment_samples_required, capture.quality);

		if (capture.samples_captured < caps.enrollment_samples_required) {
			(void)biometric_led_control(fp, BIOMETRIC_LED_OFF);
			LOG_INF("Lift finger and place again...");
			k_sleep(K_MSEC(500));
		}
	} while (capture.samples_captured < caps.enrollment_samples_required);

	(void)biometric_led_control(fp, BIOMETRIC_LED_OFF);

	ret = biometric_enroll_finalize(fp);
	if (ret == 0) {
		LOG_INF("Enrollment complete");
	} else {
		LOG_ERR("enroll_finalize failed: %d", ret);
	}

	return ret;
}

/* -------------------------------------------------------------------------
 * Biometric scan thread
 *
 * Runs BIOMETRIC_MATCH_IDENTIFY in a retry loop.  On success it updates the
 * state machine and unblocks main().  On exhaustion it sets STATE_ERROR and
 * also unblocks main() so it can report failure.
 *
 * CLEANUP HELPER 4 — scope_defer(biometric_led_off) per-iteration
 *   The LED is turned on (BREATHE) at the start of each scan attempt and
 *   guaranteed to be turned off at the end of the inner block — whether the
 *   attempt succeeds, fails with an error, or the thread returns early after
 *   a match.
 *
 * CLEANUP HELPER 5 — scope_defer(k_sem_give) after a match
 *   bio_match_sem is given before the function returns, ensuring main() is
 *   always unblocked even if future code between the state update and the
 *   k_sem_give call grows an early-return path.
 * ---------------------------------------------------------------------- */

/* The fp device pointer is set by main() before starting the thread. */
static const struct device *g_fp_dev;

static void scan_thread_fn(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	const struct device *fp = g_fp_dev;

	for (int attempt = 0; attempt < CONFIG_DEMO_SCAN_RETRIES; attempt++) {
		/*
		 * Inner block so scope_defer(biometric_led_off) fires at the
		 * end of each iteration, not at the end of the function.
		 */
		{
			/* CLEANUP HELPER 4: LED off when this block exits. */
			scope_defer(biometric_led_off)(&fp);
			(void)biometric_led_control(fp, BIOMETRIC_LED_BREATHE);

			struct biometric_match_result result = {0};
			int ret = biometric_match(fp, BIOMETRIC_MATCH_IDENTIFY, 0,
						  K_SECONDS(5), &result);

			if (ret == 0) {
				LOG_INF("Biometric match: template ID %u, confidence %d",
					result.template_id, result.confidence);

				/* CLEANUP HELPER 5: sem_give deferred so main()
				 * is always unblocked even if we add code below.
				 */
				scope_defer(k_sem_give)(&bio_match_sem);

				/* CLEANUP HELPER 6: state update under mutex. */
				{
					scope_guard(k_mutex)(&state_lock);
					g_matched_id = result.template_id;
					g_state = STATE_CONNECTING;
				}

				return;
				/* LED off  ← scope_defer(biometric_led_off)
				 * sem give ← scope_defer(k_sem_give)         */
			} else if (ret == -ENOENT) {
				LOG_WRN("No match (attempt %d/%d)", attempt + 1,
					CONFIG_DEMO_SCAN_RETRIES);
			} else if (ret == -ETIMEDOUT) {
				LOG_WRN("Scan timeout (attempt %d/%d)", attempt + 1,
					CONFIG_DEMO_SCAN_RETRIES);
			} else {
				LOG_ERR("biometric_match error: %d", ret);
			}
		} /* ← LED off here (CLEANUP HELPER 4) */

		k_sleep(K_MSEC(500));
	}

	LOG_ERR("Biometric: exceeded %d scan attempts", CONFIG_DEMO_SCAN_RETRIES);

	{
		scope_guard(k_mutex)(&state_lock);
		g_state = STATE_ERROR;
	}

	/* Unblock main() so it can report failure. */
	k_sem_give(&bio_match_sem);
}

K_THREAD_DEFINE(scan_thread, 2048, scan_thread_fn, NULL, NULL, NULL, 5, K_THREAD_NO_START, 0);

/* -------------------------------------------------------------------------
 * P2P connection
 *
 * CLEANUP HELPER 2 — scope_defer(p2p_stop_find)
 *   P2P discovery is stopped automatically when this function returns,
 *   regardless of success or failure path.
 * ---------------------------------------------------------------------- */

static int p2p_connect_to_peer(struct net_if *iface)
{
	struct wifi_p2p_params params = {0};
	int ret;

	/* Register for P2P device-found events before starting discovery. */
	net_mgmt_init_event_callback(&p2p_cb, p2p_event_handler,
				     NET_EVENT_WIFI_P2P_DEVICE_FOUND);
	net_mgmt_add_event_callback(&p2p_cb);

	/* Start discovery. */
	params.oper = WIFI_P2P_FIND;
	params.discovery_type = WIFI_P2P_FIND_START_WITH_FULL;
	params.timeout = 60; /* seconds */

	/* CLEANUP HELPER 2: stop discovery when this function exits. */
	scope_defer(p2p_stop_find)(&iface);

	ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));
	if (ret) {
		LOG_ERR("P2P find request failed: %d", ret);
		net_mgmt_del_event_callback(&p2p_cb);
		return ret;
	}

	LOG_INF("P2P discovery started — looking for peer \"%s\"",
		CONFIG_DEMO_P2P_PEER_NAME);

	/* Block until the event callback resolves the peer name to a MAC. */
	ret = k_sem_take(&peer_found_sem, K_SECONDS(30));
	if (ret == -EAGAIN) {
		LOG_ERR("Peer \"%s\" not found within 30 s", CONFIG_DEMO_P2P_PEER_NAME);
		net_mgmt_del_event_callback(&p2p_cb);
		return -ENODEV;
		/* scope_defer(p2p_stop_find) fires here */
	}

	/* Build the connect request using the dynamically resolved MAC. */
	memset(&params, 0, sizeof(params));
	params.oper = WIFI_P2P_CONNECT;
	params.connect.method = WIFI_P2P_METHOD_PBC;
	params.connect.go_intent = 7; /* neutral — let the peer decide GO role */

	{
		/* CLEANUP HELPER 3 (second use): read g_peer_mac under lock. */
		scope_guard(k_mutex)(&state_lock);
		memcpy(params.peer_addr, g_peer_mac, WIFI_MAC_ADDR_LEN);
	}

	LOG_INF("Biometric scan passed — initiating P2P connect to peer \"%s\"",
		CONFIG_DEMO_P2P_PEER_NAME);

	ret = net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &params, sizeof(params));
	if (ret) {
		LOG_ERR("P2P connect request failed: %d", ret);
		net_mgmt_del_event_callback(&p2p_cb);
		return ret;
		/* scope_defer(p2p_stop_find) fires here */
	}

	/*
	 * Wait for the connection to be established.  On timeout, tear down
	 * the group so we don't leave a half-formed P2P session alive.
	 */
	ret = k_sem_take(&p2p_connected_sem, K_SECONDS(30));
	if (ret == -EAGAIN) {
		LOG_WRN("P2P connect timed out — removing group");

		struct wifi_p2p_params remove = {.oper = WIFI_P2P_GROUP_REMOVE};

		strncpy(remove.group_remove.ifname, "p2p0",
			sizeof(remove.group_remove.ifname) - 1);
		(void)net_mgmt(NET_REQUEST_WIFI_P2P_OPER, iface, &remove, sizeof(remove));

		net_mgmt_del_event_callback(&p2p_cb);
		return -ETIMEDOUT;
		/* scope_defer(p2p_stop_find) fires here */
	}

	net_mgmt_del_event_callback(&p2p_cb);
	return 0;
	/* scope_defer(p2p_stop_find) fires here on the success path */
}

/* -------------------------------------------------------------------------
 * main
 * ---------------------------------------------------------------------- */

int main(void)
{
	LOG_INF("=== Biometric WiFi Direct Demo ===");
	LOG_INF("Peer name: \"%s\", template ID: %d, max scan retries: %d",
		CONFIG_DEMO_P2P_PEER_NAME, CONFIG_DEMO_BIOMETRIC_TEMPLATE_ID,
		CONFIG_DEMO_SCAN_RETRIES);

	/* --- Biometrics -------------------------------------------------- */

	const struct device *fp = DEVICE_DT_GET(FINGERPRINT_NODE);

	if (!device_is_ready(fp)) {
		LOG_ERR("Fingerprint sensor not ready");
		return -ENODEV;
	}

	int ret = ensure_enrolled(fp);

	if (ret < 0) {
		LOG_ERR("Enrollment failed: %d", ret);
		return ret;
	}

	/* --- Start scan thread ------------------------------------------- */

	{
		/* CLEANUP HELPER 6 (second use): guard state transition. */
		scope_guard(k_mutex)(&state_lock);
		g_state = STATE_SCANNING;
	}

	g_fp_dev = fp;
	k_thread_start(scan_thread);

	/* --- Wait for biometric result ------------------------------------ */

	k_sem_take(&bio_match_sem, K_FOREVER);

	enum demo_state s;
	{
		scope_guard(k_mutex)(&state_lock);
		s = g_state;
	}

	if (s != STATE_CONNECTING) {
		LOG_ERR("Biometric phase failed (state=%d)", (int)s);
		return -EACCES;
	}

	LOG_INF("User authenticated (template ID %u) — starting WiFi Direct", g_matched_id);

	/* --- P2P connection ----------------------------------------------- */

	struct net_if *iface = net_if_get_default();

	ret = p2p_connect_to_peer(iface);

	if (ret == 0) {
		scope_guard(k_mutex)(&state_lock);
		g_state = STATE_CONNECTED;
		LOG_INF("P2P session established");
	} else {
		scope_guard(k_mutex)(&state_lock);
		g_state = STATE_ERROR;
		LOG_ERR("P2P connection failed: %d", ret);
	}

	return ret;
}
