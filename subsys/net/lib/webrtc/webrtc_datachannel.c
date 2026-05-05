/*
 * WebRTC data channel framing.
 *
 * Implements the Zephyr-specific lightweight framing protocol that runs
 * over the established DTLS connection.  A 4-byte header precedes every
 * message:
 *
 *   byte 0–1: channel ID (big-endian uint16_t)
 *   byte 2:   message type (OPEN / ACK / DATA / CLOSE)
 *   byte 3:   reserved, must be zero
 *
 * The channel-open handshake:
 *   Initiator → OPEN (contains NUL-terminated label in payload)
 *   Remote    → ACK  (no payload)
 *
 * NOTE: This framing is not compatible with the SCTP-based RTCDataChannel
 * wire protocol defined in RFC 8832.  It exists to provide data channel
 * semantics on constrained RTOS targets without requiring a full SCTP
 * implementation.  Browser interoperability requires a future SCTP upgrade.
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

#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

struct rtc_data_channel *webrtc_dc_find_by_id(struct rtc_peer_connection *pc,
					      uint16_t id)
{
	for (int i = 0; i < CONFIG_WEBRTC_MAX_DATA_CHANNELS; i++) {
		if (pc->channels[i] != NULL && pc->channels[i]->id == id) {
			return pc->channels[i];
		}
	}
	return NULL;
}

/**
 * Send a data-channel message on the DTLS socket.
 *
 * @param pc       Peer connection.
 * @param id       Channel ID.
 * @param msg_type One of WEBRTC_DC_MSG_*.
 * @param payload  Optional payload (may be NULL when @p payload_len is 0).
 * @param payload_len  Length of @p payload.
 *
 * @return 0 on success, negative errno on failure.
 */
static int dc_send_msg(struct rtc_peer_connection *pc,
		       uint16_t id, uint8_t msg_type,
		       const uint8_t *payload, size_t payload_len)
{
	/* Maximum message size: header + label + NUL (for OPEN messages) */
	uint8_t hdr[WEBRTC_DC_HEADER_LEN];
	struct iovec iov[2];
	struct msghdr mh;
	int iovcnt = 1;
	int ret;

	hdr[0] = (uint8_t)(id >> 8U);
	hdr[1] = (uint8_t)(id & 0xFFU);
	hdr[2] = msg_type;
	hdr[3] = 0U;

	iov[0].iov_base = hdr;
	iov[0].iov_len = WEBRTC_DC_HEADER_LEN;

	if (payload != NULL && payload_len > 0U) {
		iov[1].iov_base = (void *)payload;
		iov[1].iov_len = payload_len;
		iovcnt = 2;
	}

	memset(&mh, 0, sizeof(mh));
	mh.msg_iov = iov;
	mh.msg_iovlen = iovcnt;

	ret = zsock_sendmsg(pc->dtls_sock, &mh, 0);
	if (ret < 0) {
		LOG_ERR("DC send failed: %d", errno);
		return -errno;
	}
	return 0;
}

/* -------------------------------------------------------------------------
 * Channel open handshake
 * ---------------------------------------------------------------------- */

int webrtc_dc_send_open(struct rtc_peer_connection *pc,
			struct rtc_data_channel *dc)
{
	size_t label_len = strlen(dc->label) + 1U; /* include NUL */

	return dc_send_msg(pc, dc->id, WEBRTC_DC_MSG_OPEN,
			   (const uint8_t *)dc->label, label_len);
}

/* -------------------------------------------------------------------------
 * Incoming packet demultiplexer
 * ---------------------------------------------------------------------- */

int webrtc_dc_process_packet(struct rtc_peer_connection *pc,
			     const uint8_t *buf, size_t len)
{
	struct rtc_data_channel *dc;
	uint16_t id;
	uint8_t msg_type;

	if (len < WEBRTC_DC_HEADER_LEN) {
		LOG_WRN("DC: packet too short (%zu bytes)", len);
		return -EINVAL;
	}

	id       = (uint16_t)(((uint16_t)buf[0] << 8U) | buf[1]);
	msg_type = buf[2];
	/* buf[3] reserved */

	const uint8_t *payload     = buf + WEBRTC_DC_HEADER_LEN;
	size_t         payload_len = len - WEBRTC_DC_HEADER_LEN;

	switch (msg_type) {
	case WEBRTC_DC_MSG_OPEN:
		/* Remote opened a new channel. */
		if (payload_len == 0U) {
			LOG_WRN("DC OPEN: missing label");
			return -EINVAL;
		}

		/* Allocate a new channel context for the incoming request. */
		dc = webrtc_dc_find_by_id(pc, id);
		if (dc != NULL) {
			LOG_WRN("DC OPEN: channel %u already exists", id);
			return -EEXIST;
		}

		{
			int slot = -1;

			for (int i = 0;
			     i < CONFIG_WEBRTC_MAX_DATA_CHANNELS; i++) {
				if (pc->channels[i] == NULL) {
					slot = i;
					break;
				}
			}
			if (slot < 0) {
				LOG_ERR("DC OPEN: no free channel slot");
				return -ENOMEM;
			}

			struct rtc_data_channel *new_dc =
				k_malloc(sizeof(*new_dc));

			if (new_dc == NULL) {
				return -ENOMEM;
			}

			memset(new_dc, 0, sizeof(*new_dc));
			new_dc->pc = pc;
			new_dc->id = id;
			new_dc->state = RTC_DATA_CHANNEL_CONNECTING;
			new_dc->ordered = true;
			strncpy(new_dc->label,
				(const char *)payload,
				sizeof(new_dc->label) - 1U);

			pc->channels[slot] = new_dc;
			dc = new_dc;
		}

		/* Send ACK */
		dc_send_msg(pc, id, WEBRTC_DC_MSG_ACK, NULL, 0U);
		dc->state = RTC_DATA_CHANNEL_OPEN;

		/* Notify application of the new incoming channel. */
		if (pc->cbs.on_data_channel != NULL) {
			pc->cbs.on_data_channel(pc, dc, pc->user_data);
		}
		/* Fire on_open if already registered. */
		if (dc->cbs.on_open != NULL) {
			dc->cbs.on_open(dc, dc->user_data);
		}
		break;

	case WEBRTC_DC_MSG_ACK:
		dc = webrtc_dc_find_by_id(pc, id);
		if (dc == NULL) {
			LOG_WRN("DC ACK: unknown channel %u", id);
			return -ENOENT;
		}
		dc->state = RTC_DATA_CHANNEL_OPEN;
		if (dc->cbs.on_open != NULL) {
			dc->cbs.on_open(dc, dc->user_data);
		}
		break;

	case WEBRTC_DC_MSG_DATA:
		dc = webrtc_dc_find_by_id(pc, id);
		if (dc == NULL) {
			LOG_WRN("DC DATA: unknown channel %u", id);
			return -ENOENT;
		}
		if (dc->state != RTC_DATA_CHANNEL_OPEN) {
			LOG_WRN("DC DATA: channel %u not open", id);
			return -ENOTCONN;
		}
		if (dc->cbs.on_message != NULL) {
			dc->cbs.on_message(dc, payload, payload_len,
					   dc->user_data);
		}
		break;

	case WEBRTC_DC_MSG_CLOSE:
		dc = webrtc_dc_find_by_id(pc, id);
		if (dc == NULL) {
			break;
		}
		dc->state = RTC_DATA_CHANNEL_CLOSED;
		if (dc->cbs.on_close != NULL) {
			dc->cbs.on_close(dc, dc->user_data);
		}
		/* Remove from table */
		for (int i = 0; i < CONFIG_WEBRTC_MAX_DATA_CHANNELS; i++) {
			if (pc->channels[i] == dc) {
				pc->channels[i] = NULL;
				break;
			}
		}
		k_free(dc);
		break;

	default:
		LOG_WRN("DC: unknown message type 0x%02x", msg_type);
		return -EINVAL;
	}

	return 0;
}

/* -------------------------------------------------------------------------
 * Close all channels (called during peer connection teardown)
 * ---------------------------------------------------------------------- */

void webrtc_dc_close_all(struct rtc_peer_connection *pc)
{
	for (int i = 0; i < CONFIG_WEBRTC_MAX_DATA_CHANNELS; i++) {
		struct rtc_data_channel *dc = pc->channels[i];

		if (dc == NULL) {
			continue;
		}

		if (dc->state == RTC_DATA_CHANNEL_OPEN ||
		    dc->state == RTC_DATA_CHANNEL_CONNECTING) {
			/* Best-effort close notification to peer. */
			if (pc->dtls_sock >= 0) {
				dc_send_msg(pc, dc->id, WEBRTC_DC_MSG_CLOSE,
					    NULL, 0U);
			}
			dc->state = RTC_DATA_CHANNEL_CLOSED;
			if (dc->cbs.on_close != NULL) {
				dc->cbs.on_close(dc, dc->user_data);
			}
		}

		pc->channels[i] = NULL;
		k_free(dc);
	}
}
