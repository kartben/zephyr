/*
 * WebRTC ICE agent: host candidate gathering and STUN connectivity checks.
 *
 * This implements a minimal ICE-lite-style connectivity check:
 *
 *   1. Gather host candidates by iterating network interfaces and
 *      creating one UDP socket per local IPv4/IPv6 address.
 *   2. Send STUN Binding Requests to each remote candidate as connectivity
 *      checks, using the ICE credentials exchanged in SDP.
 *   3. Nominate the first candidate pair that receives a valid Binding
 *      Success Response with the USE-CANDIDATE attribute.
 *
 * Limitations (this release):
 *   • Server-reflexive (STUN) and relayed (TURN) candidates are not
 *     gathered.  Only host candidates are supported.
 *   • Full ICE role determination is simplified: the offerer is always
 *     the controlling agent.
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
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/crc.h>

#include <psa/crypto.h>

#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * STUN helpers
 * ---------------------------------------------------------------------- */

/* Write a big-endian uint16 into a buffer and advance the pointer. */
static inline void put_be16(uint8_t **p, uint16_t v)
{
	(*p)[0] = (uint8_t)(v >> 8U);
	(*p)[1] = (uint8_t)(v & 0xFFU);
	*p += 2;
}

/* Write a big-endian uint32 into a buffer and advance the pointer. */
static inline void put_be32(uint8_t **p, uint32_t v)
{
	(*p)[0] = (uint8_t)(v >> 24U);
	(*p)[1] = (uint8_t)(v >> 16U);
	(*p)[2] = (uint8_t)(v >> 8U);
	(*p)[3] = (uint8_t)(v & 0xFFU);
	*p += 4;
}

/* Read a big-endian uint16 from a buffer. */
static inline uint16_t get_be16(const uint8_t *p)
{
	return (uint16_t)(((uint16_t)p[0] << 8U) | p[1]);
}

/* Read a big-endian uint32 from a buffer. */
static inline uint32_t get_be32(const uint8_t *p)
{
	return ((uint32_t)p[0] << 24U) |
	       ((uint32_t)p[1] << 16U) |
	       ((uint32_t)p[2] << 8U)  |
	        (uint32_t)p[3];
}

/** Check whether a received buffer looks like a STUN message. */
bool webrtc_is_stun_packet(const uint8_t *buf, size_t len)
{
	if (len < STUN_HEADER_LEN) {
		return false;
	}
	/* STUN magic cookie at bytes 4..7 */
	return get_be32(buf + 4U) == STUN_MAGIC_COOKIE;
}

/**
 * Append a STUN attribute to the message being built.
 *
 * @param p       Pointer (updated to point past the written attribute).
 * @param end     One past the end of the output buffer.
 * @param type    Attribute type.
 * @param value   Attribute value bytes.
 * @param val_len Length of @p value.
 *
 * @return 0 on success, -ENOSPC if the buffer is too small.
 */
static int stun_append_attr(uint8_t **p, const uint8_t *end,
			    uint16_t type,
			    const uint8_t *value, uint16_t val_len)
{
	/* Header: type (2) + length (2) */
	uint16_t padded = (uint16_t)((val_len + 3U) & ~3U);

	if (*p + 4U + padded > end) {
		return -ENOSPC;
	}

	put_be16(p, type);
	put_be16(p, val_len);
	memcpy(*p, value, val_len);
	if (padded > val_len) {
		memset(*p + val_len, 0, padded - val_len);
	}
	*p += padded;
	return 0;
}

/**
 * Compute HMAC-SHA1 over the STUN message up to (but not including) the
 * MESSAGE-INTEGRITY attribute, using @p key.
 *
 * @param msg        Start of the STUN message.
 * @param msg_len    Byte length of the message (up to and including the
 *                   length field, which must already account for the 24-byte
 *                   MESSAGE-INTEGRITY attribute).
 * @param key        HMAC key (ICE password).
 * @param key_len    Length of @p key.
 * @param hmac_out   20-byte output buffer.
 *
 * @return 0 on success, negative errno on failure.
 */
static int stun_hmac_sha1(const uint8_t *msg, size_t msg_len,
			  const uint8_t *key, size_t key_len,
			  uint8_t hmac_out[20])
{
	psa_status_t status;
	psa_key_id_t key_id;
	psa_key_attributes_t attrs = PSA_KEY_ATTRIBUTES_INIT;
	size_t out_len;

	psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_SIGN_MESSAGE);
	psa_set_key_algorithm(&attrs, PSA_ALG_HMAC(PSA_ALG_SHA_1));
	psa_set_key_type(&attrs, PSA_KEY_TYPE_HMAC);
	psa_set_key_bits(&attrs, (psa_key_bits_t)(key_len * 8U));

	status = psa_import_key(&attrs, key, key_len, &key_id);
	if (status != PSA_SUCCESS) {
		return -EIO;
	}

	status = psa_mac_compute(key_id,
				 PSA_ALG_HMAC(PSA_ALG_SHA_1),
				 msg, msg_len,
				 hmac_out, 20U, &out_len);
	psa_destroy_key(key_id);

	return (status == PSA_SUCCESS) ? 0 : -EIO;
}

/* -------------------------------------------------------------------------
 * STUN binding request builder
 * ---------------------------------------------------------------------- */

int webrtc_stun_send_binding_request(struct rtc_peer_connection *pc,
				     int sock,
				     const struct sockaddr *dest,
				     const char *remote_ufrag,
				     const char *remote_pwd,
				     const char *local_ufrag,
				     bool use_candidate)
{
	/* Maximum STUN request size we will ever build:
	 *   20 (header) + USERNAME(≤60) + PRIORITY(8) + ICE-CTRL(12) +
	 *   USE-CANDIDATE(4) + MESSAGE-INTEGRITY(24) + FINGERPRINT(8) ≤ 200
	 */
	uint8_t buf[200];
	uint8_t *p = buf;
	const uint8_t *end = buf + sizeof(buf);
	uint8_t txid[STUN_TRANSACTION_ID_LEN];
	char username[64];
	uint8_t hmac[20];
	uint32_t fp;
	size_t msg_base_len;
	int ret;

	/* Transaction ID */
	sys_rand_get(txid, sizeof(txid));

	/* --- STUN header placeholder (filled in after body is known) --- */
	uint8_t *hdr = p;
	put_be16(&p, STUN_MSG_BINDING_REQUEST);
	put_be16(&p, 0U);  /* length placeholder */
	put_be32(&p, STUN_MAGIC_COOKIE);
	memcpy(p, txid, STUN_TRANSACTION_ID_LEN);
	p += STUN_TRANSACTION_ID_LEN;

	/* USERNAME = "remote-ufrag:local-ufrag" */
	ret = snprintk(username, sizeof(username), "%s:%s",
		       remote_ufrag, local_ufrag);
	if (ret < 0 || (size_t)ret >= sizeof(username)) {
		return -ENOSPC;
	}
	ret = stun_append_attr(&p, end, STUN_ATTR_USERNAME,
			       (const uint8_t *)username,
			       (uint16_t)strlen(username));
	if (ret != 0) {
		return ret;
	}

	/* PRIORITY — compute per RFC 8445 §5.1.2.1 (host candidate type) */
	{
		uint32_t prio = (uint32_t)((126U << 24U) |
					   (65535U << 8U) |
					   (256U - 1U));
		uint8_t prio_bytes[4];
		uint8_t *q = prio_bytes;

		put_be32(&q, prio);
		ret = stun_append_attr(&p, end, STUN_ATTR_PRIORITY,
				       prio_bytes, sizeof(prio_bytes));
		if (ret != 0) {
			return ret;
		}
	}

	/* ICE-CONTROLLING or ICE-CONTROLLED (8 bytes = tiebreaker uint64) */
	{
		uint16_t ice_attr = pc->is_offerer
			? STUN_ATTR_ICE_CONTROLLING
			: STUN_ATTR_ICE_CONTROLLED;
		uint8_t tb[8];
		uint64_t tie = pc->ice_tiebreaker;

		tb[0] = (uint8_t)(tie >> 56U);
		tb[1] = (uint8_t)(tie >> 48U);
		tb[2] = (uint8_t)(tie >> 40U);
		tb[3] = (uint8_t)(tie >> 32U);
		tb[4] = (uint8_t)(tie >> 24U);
		tb[5] = (uint8_t)(tie >> 16U);
		tb[6] = (uint8_t)(tie >> 8U);
		tb[7] = (uint8_t)(tie & 0xFFU);

		ret = stun_append_attr(&p, end, ice_attr, tb, sizeof(tb));
		if (ret != 0) {
			return ret;
		}
	}

	/* USE-CANDIDATE (controlling agent nominates a pair) */
	if (use_candidate && pc->is_offerer) {
		ret = stun_append_attr(&p, end, STUN_ATTR_USE_CANDIDATE,
				       NULL, 0U);
		if (ret != 0) {
			return ret;
		}
	}

	/* Update length field in header to cover body so far + 24 bytes for
	 * MESSAGE-INTEGRITY that we are about to add.
	 */
	msg_base_len = (size_t)(p - hdr) - STUN_HEADER_LEN;
	hdr[2] = (uint8_t)((msg_base_len + 24U) >> 8U);
	hdr[3] = (uint8_t)((msg_base_len + 24U) & 0xFFU);

	/* MESSAGE-INTEGRITY over the full message so far (up to here). */
	ret = stun_hmac_sha1(buf, (size_t)(p - buf),
			     (const uint8_t *)remote_pwd,
			     strlen(remote_pwd), hmac);
	if (ret != 0) {
		return ret;
	}
	ret = stun_append_attr(&p, end, STUN_ATTR_MESSAGE_INTEGRITY,
			       hmac, sizeof(hmac));
	if (ret != 0) {
		return ret;
	}

	/* Update length again for the FINGERPRINT attribute (8 bytes). */
	msg_base_len = (size_t)(p - hdr) - STUN_HEADER_LEN;
	hdr[2] = (uint8_t)((msg_base_len + 8U) >> 8U);
	hdr[3] = (uint8_t)((msg_base_len + 8U) & 0xFFU);

	/* FINGERPRINT = CRC32(msg so far) XOR 0x5354554E */
	fp = crc32_ieee(buf, (size_t)(p - buf)) ^ 0x5354554EU;
	{
		uint8_t fp_bytes[4];
		uint8_t *q = fp_bytes;

		put_be32(&q, fp);
		ret = stun_append_attr(&p, end, STUN_ATTR_FINGERPRINT,
				       fp_bytes, sizeof(fp_bytes));
		if (ret != 0) {
			return ret;
		}
	}

	/* Final length update */
	msg_base_len = (size_t)(p - hdr) - STUN_HEADER_LEN;
	hdr[2] = (uint8_t)(msg_base_len >> 8U);
	hdr[3] = (uint8_t)(msg_base_len & 0xFFU);

	/* Send the message */
	socklen_t addr_len = (dest->sa_family == AF_INET)
		? sizeof(struct sockaddr_in)
		: sizeof(struct sockaddr_in6);

	ret = zsock_sendto(sock, buf, (size_t)(p - buf), 0,
			   dest, addr_len);
	if (ret < 0) {
		LOG_ERR("STUN send failed: %d", errno);
		return -errno;
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * STUN response parsing
 * ---------------------------------------------------------------------- */

/**
 * Parse a STUN Binding Success Response and extract the XOR-MAPPED-ADDRESS.
 *
 * @param buf        Received STUN message.
 * @param len        Length of @p buf.
 * @param txid       Expected 12-byte transaction ID.
 * @param mapped_out Set to the XOR-mapped address on success.
 *
 * @return 0 on success, negative errno on failure.
 */
static int stun_parse_response(const uint8_t *buf, size_t len,
			       const uint8_t *txid,
			       struct sockaddr *mapped_out)
{
	uint16_t msg_type;
	const uint8_t *attrs;
	size_t attrs_len;

	if (len < STUN_HEADER_LEN) {
		return -EINVAL;
	}

	msg_type = get_be16(buf);
	if (msg_type != STUN_MSG_BINDING_RESPONSE) {
		return -EINVAL;
	}

	if (get_be32(buf + 4U) != STUN_MAGIC_COOKIE) {
		return -EINVAL;
	}

	if (txid != NULL && memcmp(buf + 8U, txid, STUN_TRANSACTION_ID_LEN) != 0) {
		return -EINVAL;
	}

	attrs_len = (size_t)get_be16(buf + 2U);
	if (STUN_HEADER_LEN + attrs_len > len) {
		return -EINVAL;
	}

	attrs = buf + STUN_HEADER_LEN;

	while (attrs_len >= 4U) {
		uint16_t atype = get_be16(attrs);
		uint16_t alen  = get_be16(attrs + 2U);
		uint16_t padded = (uint16_t)((alen + 3U) & ~3U);
		const uint8_t *aval = attrs + 4U;

		if ((size_t)(padded + 4U) > attrs_len) {
			break;
		}

		if (atype == STUN_ATTR_XOR_MAPPED_ADDR && alen >= 8U) {
			uint8_t family = aval[1];
			uint16_t xport = get_be16(aval + 2U);
			uint16_t port = xport ^ (uint16_t)(STUN_MAGIC_COOKIE >> 16U);

			if (family == STUN_ADDR_FAMILY_IPV4 && alen >= 8U) {
				uint32_t xaddr = get_be32(aval + 4U);
				uint32_t addr = xaddr ^ STUN_MAGIC_COOKIE;
				struct sockaddr_in *sin =
					(struct sockaddr_in *)mapped_out;

				sin->sin_family = AF_INET;
				sin->sin_port = htons(port);
				sin->sin_addr.s_addr = htonl(addr);
				return 0;
			} else if (family == STUN_ADDR_FAMILY_IPV6 && alen >= 20U) {
				/* XOR with magic cookie || transaction ID */
				uint8_t xored[16];
				struct sockaddr_in6 *sin6 =
					(struct sockaddr_in6 *)mapped_out;

				xored[0] = aval[4] ^ (uint8_t)(STUN_MAGIC_COOKIE >> 24U);
				xored[1] = aval[5] ^ (uint8_t)(STUN_MAGIC_COOKIE >> 16U);
				xored[2] = aval[6] ^ (uint8_t)(STUN_MAGIC_COOKIE >> 8U);
				xored[3] = aval[7] ^ (uint8_t)(STUN_MAGIC_COOKIE);
				for (int txid_byte_idx = 0; txid_byte_idx < 12; txid_byte_idx++) {
					xored[4 + txid_byte_idx] =
						aval[8 + txid_byte_idx] ^ buf[8 + txid_byte_idx];
				}
				sin6->sin6_family = AF_INET6;
				sin6->sin6_port = htons(port);
				memcpy(&sin6->sin6_addr, xored, 16);
				return 0;
			}
		}

		attrs += (size_t)padded + 4U;
		attrs_len -= (size_t)padded + 4U;
	}

	return -ENOENT;
}

/* -------------------------------------------------------------------------
 * ICE candidate gathering
 * ---------------------------------------------------------------------- */

/**
 * Callback invoked for each network interface during candidate gathering.
 */
struct gather_ctx {
	struct rtc_peer_connection *pc;
	int ret;
};

static void gather_iface_cb(struct net_if *iface, void *user_data)
{
	struct gather_ctx *ctx = (struct gather_ctx *)user_data;
	struct rtc_peer_connection *pc = ctx->pc;
	struct net_if_config *cfg = &iface->config;

	if (ctx->ret != 0) {
		return;  /* already failed */
	}

	/* Iterate unicast IPv4 addresses */
	for (int i = 0; i < NET_IF_MAX_IPV4_ADDR; i++) {
		struct net_if_addr_ipv4 *ifaddr4;
		struct sockaddr_in sin;
		int sock;
		int ret;

		if (cfg->ip.ipv4 == NULL) {
			break;
		}
		ifaddr4 = &cfg->ip.ipv4->unicast[i];
		if (!ifaddr4->ipv4.is_used) {
			continue;
		}

		if (pc->local_candidate_count >= WEBRTC_MAX_ICE_CANDIDATES) {
			LOG_WRN("ICE: max candidates reached");
			return;
		}

		/* Open a UDP socket and bind to this address with an
		 * ephemeral port.
		 */
		sock = zsock_socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) {
			LOG_ERR("ICE: socket() failed: %d", errno);
			continue;
		}

		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = 0;
		memcpy(&sin.sin_addr,
		       &ifaddr4->ipv4.address.in_addr,
		       sizeof(sin.sin_addr));

		ret = zsock_bind(sock, (struct sockaddr *)&sin, sizeof(sin));
		if (ret < 0) {
			LOG_ERR("ICE: bind() failed: %d", errno);
			zsock_close(sock);
			continue;
		}

		/* Read back the assigned port. */
		socklen_t slen = sizeof(sin);

		ret = zsock_getsockname(sock, (struct sockaddr *)&sin, &slen);
		if (ret < 0) {
			zsock_close(sock);
			continue;
		}

		/* Store as the primary socket (reuse the first one). */
		if (pc->ice_sock < 0) {
			pc->ice_sock = sock;
		} else {
			zsock_close(sock);
		}

		struct webrtc_ice_candidate *c =
			&pc->local_candidates[pc->local_candidate_count];
		char addr_str[INET_ADDRSTRLEN];

		net_addr_ntop(AF_INET,
			      &ifaddr4->ipv4.address.in_addr,
			      addr_str, sizeof(addr_str));
		strncpy(c->addr, addr_str, sizeof(c->addr) - 1U);
		c->port = ntohs(sin.sin_port);
		c->component = 1U;
		/* Priority per RFC 8445 §5.1.2.1 for host candidates */
		c->priority = (uint32_t)((126U << 24U) |
					 (65535U << 8U) |
					 (uint32_t)(256U - 1U));
		c->valid = true;
		pc->local_candidate_count++;

		LOG_DBG("ICE: gathered host candidate %s:%u",
			c->addr, c->port);

		/* Copy the candidate into the local SDP info. */
		memcpy(&pc->local_sdp.candidates[pc->local_sdp.candidate_count],
		       c, sizeof(*c));
		pc->local_sdp.candidate_count++;
	}
}

int webrtc_ice_gather_candidates(struct rtc_peer_connection *pc)
{
	struct gather_ctx ctx = {
		.pc = pc,
		.ret = 0,
	};

	pc->local_candidate_count = 0U;
	pc->local_sdp.candidate_count = 0U;
	pc->ice_sock = -1;

	sys_rand_get(&pc->ice_tiebreaker, sizeof(pc->ice_tiebreaker));

	net_if_foreach(gather_iface_cb, &ctx);

	if (pc->local_candidate_count == 0U) {
		LOG_ERR("ICE: no host candidates found");
		return -ENONET;
	}

	LOG_INF("ICE: gathered %u host candidate(s)",
		pc->local_candidate_count);
	return 0;
}

/* -------------------------------------------------------------------------
 * ICE connectivity checks
 * ---------------------------------------------------------------------- */

int webrtc_ice_start_checks(struct rtc_peer_connection *pc)
{
	uint8_t i;
	int ret;

	if (pc->ice_sock < 0) {
		return -ENOTCONN;
	}

	/* Send a binding request with USE-CANDIDATE to each remote candidate
	 * for which we have address information.
	 */
	for (i = 0U; i < pc->remote_sdp.candidate_count; i++) {
		const struct webrtc_ice_candidate *rc =
			&pc->remote_sdp.candidates[i];
		struct sockaddr dest;
		struct sockaddr_in *sin = (struct sockaddr_in *)&dest;

		if (!rc->valid) {
			continue;
		}

		memset(&dest, 0, sizeof(dest));

		if (zsock_inet_pton(AF_INET, rc->addr,
				    &sin->sin_addr) == 1) {
			sin->sin_family = AF_INET;
			sin->sin_port = htons(rc->port);
		} else {
			struct sockaddr_in6 *sin6 =
				(struct sockaddr_in6 *)&dest;

			if (zsock_inet_pton(AF_INET6, rc->addr,
					    &sin6->sin6_addr) != 1) {
				LOG_WRN("ICE: cannot parse remote addr %s",
					rc->addr);
				continue;
			}
			sin6->sin6_family = AF_INET6;
			sin6->sin6_port = htons(rc->port);
		}

		ret = webrtc_stun_send_binding_request(
			pc, pc->ice_sock, &dest,
			pc->remote_sdp.ice_ufrag,
			pc->remote_sdp.ice_pwd,
			pc->local_sdp.ice_ufrag,
			true);
		if (ret != 0) {
			LOG_WRN("ICE: check to %s:%u failed: %d",
				rc->addr, rc->port, ret);
		} else {
			LOG_DBG("ICE: binding request sent to %s:%u",
				rc->addr, rc->port);
		}
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Incoming STUN packet handler
 * ---------------------------------------------------------------------- */

void webrtc_ice_process_packet(struct rtc_peer_connection *pc,
			       const uint8_t *buf, size_t len,
			       const struct sockaddr *from)
{
	uint16_t msg_type;

	if (len < STUN_HEADER_LEN) {
		return;
	}

	msg_type = get_be16(buf);

	if (msg_type == STUN_MSG_BINDING_RESPONSE) {
		struct sockaddr mapped;

		if (stun_parse_response(buf, len, NULL, &mapped) == 0) {
			/* A response was received; nominate this pair. */
			if (pc->selected_remote_idx < 0) {
				const char *src_addr;
				char addr_str[INET6_ADDRSTRLEN];

				if (from->sa_family == AF_INET) {
					net_addr_ntop(AF_INET,
					    &net_sin((struct sockaddr *)from)->sin_addr,
					    addr_str, sizeof(addr_str));
				} else {
					net_addr_ntop(AF_INET6,
					    &net_sin6((struct sockaddr *)from)->sin6_addr,
					    addr_str, sizeof(addr_str));
				}
				src_addr = addr_str;

				for (int i = 0;
				     i < pc->remote_sdp.candidate_count;
				     i++) {
					if (strcmp(pc->remote_sdp
						       .candidates[i].addr,
						   src_addr) == 0) {
						pc->selected_remote_idx =
							(int8_t)i;
						pc->selected_local_idx = 0;
						LOG_INF("ICE: nominated pair "
							"with %s:%u",
							src_addr,
							pc->remote_sdp
							    .candidates[i]
							    .port);
						break;
					}
				}
			}
		}
	} else if (msg_type == STUN_MSG_BINDING_REQUEST) {
		/* Respond with a simple Binding Success Response.  A full
		 * implementation would validate MESSAGE-INTEGRITY here.
		 */
		uint8_t resp[STUN_HEADER_LEN + 4U + 8U]; /* hdr + attr hdr + addr */
		uint8_t *p = resp;
		const uint8_t *end = resp + sizeof(resp);

		put_be16(&p, STUN_MSG_BINDING_RESPONSE);
		put_be16(&p, 0U);  /* length placeholder */
		put_be32(&p, STUN_MAGIC_COOKIE);
		memcpy(p, buf + 8U, STUN_TRANSACTION_ID_LEN);
		p += STUN_TRANSACTION_ID_LEN;

		if (from->sa_family == AF_INET) {
			uint8_t attr[8];
			uint8_t *q = attr;
			uint32_t xaddr = ntohl(net_sin(
				(struct sockaddr *)from)->sin_addr.s_addr)
				^ STUN_MAGIC_COOKIE;
			uint16_t xport = ntohs(net_sin(
				(struct sockaddr *)from)->sin_port)
				^ (uint16_t)(STUN_MAGIC_COOKIE >> 16U);

			q[0] = 0U;
			q[1] = STUN_ADDR_FAMILY_IPV4;
			q[2] = (uint8_t)(xport >> 8U);
			q[3] = (uint8_t)(xport & 0xFFU);
			q += 4;
			put_be32(&q, xaddr);

			stun_append_attr(&p, end,
					 STUN_ATTR_XOR_MAPPED_ADDR,
					 attr, sizeof(attr));
		}

		/* Update length */
		resp[2] = (uint8_t)(((size_t)(p - resp) - STUN_HEADER_LEN) >> 8U);
		resp[3] = (uint8_t)(((size_t)(p - resp) - STUN_HEADER_LEN) & 0xFFU);

		socklen_t alen = (from->sa_family == AF_INET)
			? sizeof(struct sockaddr_in)
			: sizeof(struct sockaddr_in6);

		zsock_sendto(pc->ice_sock, resp, (size_t)(p - resp), 0,
			     from, alen);
	}
}
