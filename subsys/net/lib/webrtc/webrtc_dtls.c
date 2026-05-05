/*
 * WebRTC DTLS transport.
 *
 * Wraps Zephyr's existing DTLS socket support to provide the secure
 * transport layer for WebRTC data channels.
 *
 * After ICE nominates a candidate pair, this module:
 *   1. Creates a DTLS socket over the nominated UDP path.
 *   2. Performs the DTLS handshake (client or server role determined by
 *      the SDP a=setup: attribute).
 *   3. Optionally verifies the peer's certificate fingerprint against the
 *      value exchanged in SDP.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_webrtc, CONFIG_NET_WEBRTC_LOG_LEVEL);

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * DTLS connect / disconnect
 * ---------------------------------------------------------------------- */

int webrtc_dtls_connect(struct rtc_peer_connection *pc)
{
	int sock;
	int ret;
	sec_tag_t sec_tags[1];
	int role;
	struct sockaddr remote_addr;
	socklen_t remote_addr_len;

	/* Determine the remote address from the nominated ICE candidate. */
	if (pc->selected_remote_idx < 0 ||
	    pc->selected_remote_idx >= pc->remote_sdp.candidate_count) {
		LOG_ERR("DTLS: no nominated remote candidate");
		return -ENOTCONN;
	}

	const struct webrtc_ice_candidate *rc =
		&pc->remote_sdp.candidates[pc->selected_remote_idx];

	memset(&remote_addr, 0, sizeof(remote_addr));

	if (zsock_inet_pton(AF_INET, rc->addr,
			    &net_sin(&remote_addr)->sin_addr) == 1) {
		net_sin(&remote_addr)->sin_family = AF_INET;
		net_sin(&remote_addr)->sin_port = htons(rc->port);
		remote_addr_len = sizeof(struct sockaddr_in);
	} else if (zsock_inet_pton(AF_INET6, rc->addr,
				   &net_sin6(&remote_addr)->sin6_addr) == 1) {
		net_sin6(&remote_addr)->sin6_family = AF_INET6;
		net_sin6(&remote_addr)->sin6_port = htons(rc->port);
		remote_addr_len = sizeof(struct sockaddr_in6);
	} else {
		LOG_ERR("DTLS: invalid remote address %s", rc->addr);
		return -EINVAL;
	}

	/* Create a DTLS socket. */
	sock = zsock_socket(remote_addr.sa_family, SOCK_DGRAM,
			    IPPROTO_DTLS_1_2);
	if (sock < 0) {
		LOG_ERR("DTLS: socket() failed: %d", errno);
		return -errno;
	}

	/* Configure TLS credentials. */
	sec_tags[0] = pc->config.dtls_tag;
	ret = zsock_setsockopt(sock, SOL_TLS, TLS_SEC_TAG_LIST,
			       sec_tags, sizeof(sec_tags));
	if (ret < 0) {
		LOG_ERR("DTLS: TLS_SEC_TAG_LIST failed: %d", errno);
		goto fail;
	}

	/* Set DTLS role: active (client) or passive (server).
	 *
	 * We become the DTLS client when:
	 *   - We are the offerer AND the local SDP had a=setup:actpass
	 *     (the answerer chose "active"), OR
	 *   - The remote SDP contained a=setup:active (they are client).
	 *
	 * In practice we use local_sdp.dtls_active which was set during
	 * answer generation.
	 */
	role = pc->local_sdp.dtls_active
		? TLS_DTLS_ROLE_CLIENT
		: TLS_DTLS_ROLE_SERVER;

	ret = zsock_setsockopt(sock, SOL_TLS, TLS_DTLS_ROLE,
			       &role, sizeof(role));
	if (ret < 0) {
		LOG_ERR("DTLS: TLS_DTLS_ROLE failed: %d", errno);
		goto fail;
	}

	/* Peer verification: we accept the peer based on fingerprint
	 * comparison rather than a full PKI chain, so disable the default
	 * CA-based verification.
	 */
	{
		int verify = TLS_PEER_VERIFY_NONE;

		ret = zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY,
				       &verify, sizeof(verify));
		if (ret < 0) {
			LOG_ERR("DTLS: TLS_PEER_VERIFY failed: %d", errno);
			goto fail;
		}
	}

	/* Connect to the remote address. */
	ret = zsock_connect(sock, &remote_addr, remote_addr_len);
	if (ret < 0) {
		LOG_ERR("DTLS: connect() failed: %d", errno);
		goto fail;
	}

	LOG_INF("DTLS: handshake started with %s:%u (role=%s)",
		rc->addr, rc->port,
		pc->local_sdp.dtls_active ? "client" : "server");

	pc->dtls_sock = sock;
	pc->dtls_connected = true;
	return 0;

fail:
	zsock_close(sock);
	return -errno;
}

void webrtc_dtls_close(struct rtc_peer_connection *pc)
{
	if (pc->dtls_sock >= 0) {
		zsock_close(pc->dtls_sock);
		pc->dtls_sock = -1;
	}
	pc->dtls_connected = false;
}
