/*
 * SPDX-License-Identifier: Apache-2.0
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SecureVault Node — secure telemetry reporting module
 *
 * ┌─────────────────────────────────────────────────────────────────────────┐
 * │  Zephyr 4.4 features showcased in this file                            │
 * │                                                                         │
 * │  1. WireGuard VPN  (include/zephyr/net/wireguard.h)                    │
 * │     Full WireGuard VPN stack on bare metal.  A virtual network        │
 * │     interface is created; UDP traffic routed through it is             │
 * │     transparently encrypted with Curve25519 / ChaCha20-Poly1305.      │
 * │     The application just opens a normal UDP socket to the peer's VPN  │
 * │     IP — the encryption is invisible at the socket layer.             │
 * │                                                                         │
 * │  2. Scope-based Cleanup  (include/zephyr/cleanup/kernel.h)             │
 * │     scope_defer(zsock_close) ensures the UDP socket is always closed   │
 * │     regardless of which return path is taken.  Classic C would need   │
 * │     a manual close on every error branch.                              │
 * └─────────────────────────────────────────────────────────────────────────┘
 *
 * Guard the whole file with CONFIG_WIREGUARD.  When the module is absent the
 * header provides inline no-ops so the rest of the app still links cleanly.
 */

#include "report.h"
#include "monitor.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(report, LOG_LEVEL_INF);

#if defined(CONFIG_WIREGUARD)

#include <stdio.h>
#include <string.h>

#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/virtual.h>
#include <zephyr/net/virtual_mgmt.h>
#include <zephyr/net/wireguard.h>
#include <zephyr/sys/base64.h>

/*
 * [4.4 NEW] Scope-based cleanup — register a defer for zsock_close.
 *
 * After this macro, anywhere in this compilation unit we can write:
 *   scope_defer(zsock_close)(sock);
 * …and zsock_close(sock) will be called when the enclosing scope exits.
 */
#include <zephyr/cleanup/kernel.h>
SCOPE_DEFER_DEFINE(zsock_close, int);

/* -------------------------------------------------------------------------
 * Demo key material — generated with `wg genkey | wg pubkey`.
 * REPLACE THESE before deploying in production.
 * ---------------------------------------------------------------------- */

/* Private key of this device (base64, 32-byte Curve25519 scalar). */
#define MY_PRIVATE_KEY  "lmAIbJR8PQOpgJxmfOydBiDbexTMEKsjglZ1sj3kIjs="

/* Public key of the facility management gateway peer. */
#define PEER_PUBLIC_KEY "v/xiHtIDhnnMJV3SbqI+cChcSqfrU4zlhLUUbL1J8x4="

/* WireGuard endpoint of the peer (IP as string for inet_pton). */
#define PEER_ENDPOINT_IP   "192.0.2.2"
#define PEER_ENDPOINT_PORT 51820

/* Tunnel IP address assigned to this device inside the VPN. */
#define MY_VPN_ADDR "198.51.100.1"

/* Peer IP reachable over the tunnel (what we send UDP to). */
#define PEER_VPN_IP   "198.51.100.2"
#define PEER_VPN_PORT 9000

/* Allowed peer subnet over the VPN. */
#define PEER_ALLOWED_IP      "198.51.100.0"
#define PEER_ALLOWED_MASKLEN 24

/* -------------------------------------------------------------------------
 * VPN interface reference — stored after successful init.
 * ---------------------------------------------------------------------- */
static struct net_if *vpn_iface;

/* -------------------------------------------------------------------------
 * Interface enumeration callback — find the WireGuard virtual interface.
 * ---------------------------------------------------------------------- */
static void find_vpn_iface(struct net_if *iface, void *user_data)
{
	struct net_if **out = user_data;

	if (*out != NULL) {
		return; /* already found */
	}

	if (net_if_l2(iface) != &NET_L2_GET_NAME(VIRTUAL)) {
		return;
	}

	if (net_virtual_get_iface_capabilities(iface) & VIRTUAL_INTERFACE_VPN) {
		*out = iface;
	}
}

/* -------------------------------------------------------------------------
 * WireGuard interface setup.
 *
 * Adapts the pattern from samples/net/wireguard/src/wg.c to our scenario.
 * Key steps:
 *   1. Find the VPN virtual interface created by CONFIG_WIREGUARD=y.
 *   2. Install our Curve25519 private key via net_mgmt.
 *   3. Assign our tunnel IP address to the interface.
 *   4. Configure the peer (public key + allowed IPs + endpoint) via
 *      wireguard_peer_add() — the new 4.4 WireGuard API.
 *   5. Bring the interface up.
 * ---------------------------------------------------------------------- */
static int setup_wireguard(void)
{
	int ret;

	/* Step 1: find the WireGuard virtual interface. */
	net_if_foreach(find_vpn_iface, &vpn_iface);
	if (!vpn_iface) {
		LOG_ERR("No WireGuard VPN interface found — is CONFIG_WIREGUARD=y?");
		return -ENODEV;
	}

	/* Step 2: install our private key. */
	struct virtual_interface_req_params params = {0};
	uint8_t private_key[NET_VIRTUAL_MAX_PUBLIC_KEY_LEN];
	size_t olen;

	ret = base64_decode(private_key, sizeof(private_key), &olen,
			    MY_PRIVATE_KEY, strlen(MY_PRIVATE_KEY));
	if (ret < 0) {
		LOG_ERR("base64_decode private key failed: %d", ret);
		return ret;
	}
	params.private_key.data = private_key;
	params.private_key.len  = olen;

	ret = net_mgmt(NET_REQUEST_VIRTUAL_INTERFACE_SET_PRIVATE_KEY,
		       vpn_iface, &params, sizeof(params));
	memset(private_key, 0, sizeof(private_key)); /* zero key material */
	if (ret < 0) {
		LOG_ERR("Cannot set private key: %d", ret);
		return ret;
	}

	/* Step 3: assign our tunnel IPv4 address. */
	struct in_addr my_addr;

	zsock_inet_pton(AF_INET, MY_VPN_ADDR, &my_addr);
	if (!net_if_ipv4_addr_add(vpn_iface, &my_addr, NET_ADDR_MANUAL, 0)) {
		LOG_ERR("Cannot add tunnel IP address");
		return -ENOMEM;
	}

	/* Step 4: configure the peer.
	 *
	 * wireguard_peer_add() is part of the new 4.4 WireGuard API.
	 * It registers the peer's public key, endpoint, and allowed IP
	 * ranges with the WireGuard subsystem.
	 */
	struct wireguard_peer_config peer = {0};

	/* Public key in base64 — wireguard_peer_config takes a string pointer. */
	peer.public_key = PEER_PUBLIC_KEY;

	/* Peer endpoint address (where to send encrypted UDP packets). */
	struct sockaddr_in *ep = (struct sockaddr_in *)&peer.endpoint_ip;

	ep->sin_family = AF_INET;
	ep->sin_port   = htons(PEER_ENDPOINT_PORT);
	zsock_inet_pton(AF_INET, PEER_ENDPOINT_IP, &ep->sin_addr);

	/* Allowed IPs — traffic to this subnet will be routed through the tunnel. */
	peer.allowed_ip[0].is_valid = true;
	peer.allowed_ip[0].mask_len = PEER_ALLOWED_MASKLEN;
	peer.allowed_ip[0].addr.family = AF_INET;
	zsock_inet_pton(AF_INET, PEER_ALLOWED_IP,
			&peer.allowed_ip[0].addr.in_addr);

	struct net_if *peer_iface = NULL;

	ret = wireguard_peer_add(&peer, &peer_iface);
	if (ret < 0) {
		LOG_ERR("wireguard_peer_add failed: %d", ret);
		return ret;
	}

	/* Step 5: bring the interface up. */
	net_if_up(vpn_iface);

	LOG_INF("WireGuard VPN ready — tunnel %s → peer %s:%d",
		MY_VPN_ADDR, PEER_ENDPOINT_IP, PEER_ENDPOINT_PORT);
	LOG_INF("  (traffic to %s/%d encrypted transparently)",
		PEER_ALLOWED_IP, PEER_ALLOWED_MASKLEN);

	return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------- */

int report_init(void)
{
	return setup_wireguard();
}

int report_send(const struct monitor_reading *reading, uint32_t auth_events)
{
	char payload[128];
	int  payload_len;

	payload_len = snprintf(payload, sizeof(payload),
			       "{\"seq\":%u,\"temp\":%d.%03d,"
			       "\"hum\":%d.%03d,\"auth_events\":%u}",
			       reading->seq,
			       reading->temp_mc / 1000,
			       abs(reading->temp_mc  % 1000),
			       reading->hum_mpct / 1000,
			       abs(reading->hum_mpct % 1000),
			       auth_events);

	/*
	 * Open a UDP socket to the peer's VPN IP.
	 * From the application's perspective this is a completely ordinary
	 * UDP socket — the WireGuard virtual interface handles encryption
	 * and decryption transparently.
	 */
	int sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (sock < 0) {
		LOG_ERR("socket() failed: %d", errno);
		return -errno;
	}

	/*
	 * [4.4 NEW] scope_defer(zsock_close) — the socket will be closed
	 * automatically when report_send() returns, regardless of whether
	 * we exit via the sendto error path or the happy path below.
	 *
	 * Without this we'd need:
	 *   if (ret < 0) { zsock_close(sock); return ret; }
	 * on every subsequent error branch — easy to forget one.
	 */
	scope_defer(zsock_close)(sock);

	struct sockaddr_in dest = {
		.sin_family = AF_INET,
		.sin_port   = htons(PEER_VPN_PORT),
	};

	zsock_inet_pton(AF_INET, PEER_VPN_IP, &dest.sin_addr);

	int ret = zsock_sendto(sock, payload, payload_len, 0,
			       (struct sockaddr *)&dest, sizeof(dest));

	if (ret < 0) {
		LOG_ERR("sendto() failed: %d", errno);
		return -errno; /* zsock_close called by scope_defer */
	}

	LOG_INF("Telemetry sent (%d bytes): %s", payload_len, payload);

	/* zsock_close(sock) called here by scope_defer */
	return 0;
}

#else /* CONFIG_WIREGUARD not set */

int report_init(void)
{
	LOG_INF("WireGuard not enabled — reporting disabled. "
		"Build with CONFIG_WIREGUARD=y to activate.");
	return 0;
}

int report_send(const struct monitor_reading *reading, uint32_t auth_events)
{
	LOG_INF("[STUB] Would report seq=%u temp=%d.%03d hum=%d.%03d auth=%u",
		reading->seq,
		reading->temp_mc / 1000,  abs(reading->temp_mc  % 1000),
		reading->hum_mpct / 1000, abs(reading->hum_mpct % 1000),
		auth_events);
	return 0;
}

#endif /* CONFIG_WIREGUARD */
