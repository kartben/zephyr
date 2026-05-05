/*
 * WebRTC SDP parsing and generation.
 *
 * Generates and parses a minimal SDP subset covering exactly what is
 * needed for WebRTC data-channel negotiation:
 *
 *   • Session-level lines  (v=, o=, s=, t=)
 *   • Grouping             (a=group:BUNDLE data)
 *   • Data m-section       (m=application … UDP/DTLS webrtc-datachannel-zephyr)
 *   • Connection           (c=IN IP4/IP6 <addr>)
 *   • ICE credentials      (a=ice-ufrag:, a=ice-pwd:)
 *   • DTLS fingerprint     (a=fingerprint:sha-256 …)
 *   • DTLS setup role      (a=setup:active|passive|actpass)
 *   • Media ID             (a=mid:data)
 *   • ICE candidates       (a=candidate:…)
 *
 * NOTE: the m= protocol token "UDP/DTLS/webrtc-datachannel-zephyr" signals
 * that the data framing is the Zephyr-specific lightweight protocol rather
 * than standard SCTP-over-DTLS.  Endpoints that understand only the W3C
 * WebRTC specification will reject this m-section.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(net_webrtc, CONFIG_NET_WEBRTC_LOG_LEVEL);

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/random/random.h>
#include <zephyr/net/net_ip.h>

#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/**
 * Append a formatted string to @p buf.
 *
 * @param buf     Output buffer.
 * @param buf_len Total buffer size.
 * @param offset  Current write position (updated on success).
 * @param fmt     printf-style format string.
 *
 * @return 0 on success, -ENOSPC if the buffer would overflow.
 */
static int sdp_append(char *buf, size_t buf_len, size_t *offset,
		      const char *fmt, ...)
{
	va_list ap;
	int written;

	va_start(ap, fmt);
	written = vsnprintk(buf + *offset, buf_len - *offset, fmt, ap);
	va_end(ap);

	if (written < 0 || (size_t)written >= buf_len - *offset) {
		return -ENOSPC;
	}

	*offset += (size_t)written;
	return 0;
}

/**
 * Format a raw SHA-256 digest as a colon-separated hex string.
 *
 * @param digest  32-byte input.
 * @param out     Output buffer (at least WEBRTC_FINGERPRINT_STR_LEN bytes).
 */
static void format_fingerprint(const uint8_t *digest, char *out)
{
	size_t i;

	for (i = 0U; i < WEBRTC_FINGERPRINT_LEN; i++) {
		if (i > 0U) {
			*out++ = ':';
		}
		out += snprintk(out, 3, "%02X", digest[i]);
	}
}

/**
 * Parse a colon-separated hex fingerprint string into raw bytes.
 *
 * @param str   Input string (e.g. "AB:CD:EF:…").
 * @param out   Output buffer (WEBRTC_FINGERPRINT_LEN bytes).
 *
 * @return 0 on success, -EINVAL on malformed input.
 */
static int parse_fingerprint(const char *str, uint8_t *out)
{
	size_t i;

	for (i = 0U; i < WEBRTC_FINGERPRINT_LEN; i++) {
		unsigned int byte;
		int consumed;

		if (sscanf(str, "%2x%n", &byte, &consumed) != 1) {
			return -EINVAL;
		}
		out[i] = (uint8_t)byte;
		str += consumed;
		if (i < WEBRTC_FINGERPRINT_LEN - 1U && *str == ':') {
			str++;
		}
	}
	return 0;
}

/* -------------------------------------------------------------------------
 * SDP generation
 * ---------------------------------------------------------------------- */

/**
 * Build the common parts of an SDP offer or answer into @p buf.
 *
 * @param pc       Peer connection owning @p local.
 * @param local    Local SDP info (credentials, fingerprint, candidates).
 * @param buf      Output buffer.
 * @param buf_len  Size of @p buf.
 * @param is_offer True when generating an offer; false for an answer.
 *
 * @return 0 on success, negative errno on failure.
 */
static int sdp_generate(struct rtc_peer_connection *pc,
			const struct webrtc_sdp_info *local,
			char *buf, size_t buf_len,
			bool is_offer)
{
	size_t off = 0U;
	char fp_str[WEBRTC_FINGERPRINT_STR_LEN];
	const char *setup_str;
	uint32_t sess_id;
	int ret;

	/* Compute setup role:
	 *   Offerer → "actpass" (can be either; answerer decides)
	 *   Answerer → "active" (we drive the DTLS handshake) unless the
	 *              remote SDP contained "active", in which case we use
	 *              "passive".
	 */
	if (is_offer) {
		setup_str = "actpass";
	} else if (pc->remote_sdp.dtls_active) {
		/* Remote is already active; we must be passive. */
		setup_str = "passive";
	} else {
		setup_str = "active";
	}

	sys_rand_get(&sess_id, sizeof(sess_id));
	format_fingerprint(local->fingerprint, fp_str);

	/* v= o= s= t= */
	ret = sdp_append(buf, buf_len, &off,
			 "v=0\r\n"
			 "o=- %u 2 IN IP4 127.0.0.1\r\n"
			 "s=-\r\n"
			 "t=0 0\r\n",
			 sess_id);
	if (ret != 0) {
		return ret;
	}

	/* BUNDLE grouping */
	ret = sdp_append(buf, buf_len, &off, "a=group:BUNDLE data\r\n");
	if (ret != 0) {
		return ret;
	}

	/* m= line */
	ret = sdp_append(buf, buf_len, &off,
			 "m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
			 "webrtc-datachannel\r\n"
			 "c=IN IP4 0.0.0.0\r\n");
	if (ret != 0) {
		return ret;
	}

	/* ICE credentials */
	ret = sdp_append(buf, buf_len, &off,
			 "a=ice-ufrag:%s\r\n"
			 "a=ice-pwd:%s\r\n"
			 "a=ice-options:trickle\r\n",
			 local->ice_ufrag,
			 local->ice_pwd);
	if (ret != 0) {
		return ret;
	}

	/* DTLS fingerprint and setup role */
	ret = sdp_append(buf, buf_len, &off,
			 "a=fingerprint:sha-256 %s\r\n"
			 "a=setup:%s\r\n",
			 fp_str,
			 setup_str);
	if (ret != 0) {
		return ret;
	}

	/* mid */
	ret = sdp_append(buf, buf_len, &off, "a=mid:data\r\n");
	if (ret != 0) {
		return ret;
	}

	/* ICE host candidates (if already gathered) */
	for (uint8_t i = 0U; i < local->candidate_count; i++) {
		const struct webrtc_ice_candidate *c = &local->candidates[i];

		if (!c->valid) {
			continue;
		}
		ret = sdp_append(buf, buf_len, &off,
				 "a=candidate:%u %u UDP %u %s %u typ host\r\n",
				 (unsigned int)i + 1U,
				 (unsigned int)c->component,
				 (unsigned int)c->priority,
				 c->addr,
				 (unsigned int)c->port);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

int webrtc_sdp_generate_offer(struct rtc_peer_connection *pc,
			      char *buf, size_t buf_len)
{
	return sdp_generate(pc, &pc->local_sdp, buf, buf_len, true);
}

int webrtc_sdp_generate_answer(struct rtc_peer_connection *pc,
			       char *buf, size_t buf_len)
{
	return sdp_generate(pc, &pc->local_sdp, buf, buf_len, false);
}

/* -------------------------------------------------------------------------
 * SDP parsing
 * ---------------------------------------------------------------------- */

/**
 * Extract the value after a given prefix on an SDP line.
 *
 * @param line    NUL-terminated SDP line (stripped of the trailing CRLF).
 * @param prefix  The expected prefix to match.
 *
 * @return Pointer to the first character after @p prefix, or NULL if the
 *         line does not start with @p prefix.
 */
static const char *sdp_line_value(const char *line, const char *prefix)
{
	size_t plen = strlen(prefix);

	if (strncmp(line, prefix, plen) == 0) {
		return line + plen;
	}
	return NULL;
}

int webrtc_sdp_parse(const char *sdp, struct webrtc_sdp_info *out)
{
	char line[256];
	const char *p = sdp;
	const char *val;

	memset(out, 0, sizeof(*out));

	while (*p != '\0') {
		/* Extract one line (up to \r\n or \n). */
		size_t len = 0U;

		while (*p != '\0' && *p != '\r' && *p != '\n') {
			if (len < sizeof(line) - 1U) {
				line[len++] = *p;
			}
			p++;
		}
		line[len] = '\0';

		/* Skip line endings. */
		if (*p == '\r') {
			p++;
		}
		if (*p == '\n') {
			p++;
		}

		if (len == 0U) {
			continue;
		}

		/* Parse ice-ufrag */
		val = sdp_line_value(line, "a=ice-ufrag:");
		if (val != NULL) {
			strncpy(out->ice_ufrag, val,
				sizeof(out->ice_ufrag) - 1U);
			continue;
		}

		/* Parse ice-pwd */
		val = sdp_line_value(line, "a=ice-pwd:");
		if (val != NULL) {
			strncpy(out->ice_pwd, val,
				sizeof(out->ice_pwd) - 1U);
			continue;
		}

		/* Parse fingerprint (only sha-256 is supported) */
		val = sdp_line_value(line, "a=fingerprint:sha-256 ");
		if (val != NULL) {
			if (parse_fingerprint(val,
					      out->fingerprint) == 0) {
				out->fingerprint_valid = true;
			}
			continue;
		}

		/* Parse setup role */
		val = sdp_line_value(line, "a=setup:");
		if (val != NULL) {
			/* "active" → remote is DTLS client */
			out->dtls_active = (strcmp(val, "active") == 0);
			continue;
		}

		/* Parse a=candidate:... lines */
		val = sdp_line_value(line, "a=candidate:");
		if (val != NULL &&
		    out->candidate_count < WEBRTC_MAX_ICE_CANDIDATES) {
			struct webrtc_ice_candidate *c =
				&out->candidates[out->candidate_count];
			unsigned int foundation, component, priority, port;
			char addr[INET6_ADDRSTRLEN];
			char typ[16];

			if (sscanf(val,
				   "%u %u UDP %u %39s %u typ %15s",
				   &foundation, &component,
				   &priority, addr, &port, typ) == 6 &&
			    strcmp(typ, "host") == 0) {
				strncpy(c->addr, addr,
					sizeof(c->addr) - 1U);
				c->port = (uint16_t)port;
				c->component = (uint8_t)component;
				c->priority = priority;
				c->valid = true;
				out->candidate_count++;
			}
			continue;
		}
	}

	/* Validate mandatory fields. */
	if (out->ice_ufrag[0] == '\0' ||
	    out->ice_pwd[0] == '\0') {
		LOG_ERR("SDP missing ICE credentials");
		return -EINVAL;
	}

	if (!out->fingerprint_valid) {
		LOG_ERR("SDP missing DTLS fingerprint");
		return -EINVAL;
	}

	return 0;
}
