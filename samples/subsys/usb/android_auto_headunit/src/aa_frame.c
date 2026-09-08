/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_frame.h"

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "aa_ids.h"
#include "aa_mem.h"
#include "aa_tls.h"
#include "aa_transport.h"

LOG_MODULE_REGISTER(aa_frame, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/* Largest plaintext handed to the TLS layer per frame */
#define TX_CHUNK_SIZE CONFIG_SAMPLE_AA_HU_TX_CHUNK_SIZE
/* TLS record overhead with AES-GCM: header, explicit nonce and tag */
#define TLS_RECORD_OVERHEAD 64U

BUILD_ASSERT(TX_CHUNK_SIZE <= CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN,
	     "TX chunk must fit one TLS record");
BUILD_ASSERT(CONFIG_SAMPLE_AA_HU_RX_FRAME_SIZE >= CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN + 29,
	     "RX frame buffer must hold a full TLS record");

static K_MUTEX_DEFINE(tx_lock);

static uint8_t tx_plain[TX_CHUNK_SIZE];
static uint8_t tx_frame[AA_FRAME_MAX_HDR + 2U + TX_CHUNK_SIZE + TLS_RECORD_OVERHEAD];
static uint8_t rx_frame[CONFIG_SAMPLE_AA_HU_RX_FRAME_SIZE] AA_HU_BIG_BUF;

static bool encrypted;
static size_t tx_chunk = TX_CHUNK_SIZE;

/* Frame being assembled: header fields applied by aa_frame_tx_record() */
static struct {
	uint8_t channel;
	uint8_t flags;
	uint32_t total;
	bool has_total;
} tx_hdr;

/* Streamed message state */
static struct {
	bool active;
	uint8_t channel;
	size_t total;
	size_t written;
	size_t plain_len;
	bool first_sent;
} stream;

int aa_frame_init(void)
{
	return 0;
}

void aa_frame_lock(void)
{
	k_mutex_lock(&tx_lock, K_FOREVER);
}

void aa_frame_unlock(void)
{
	k_mutex_unlock(&tx_lock);
}

void aa_frame_reset(void)
{
	aa_frame_lock();
	encrypted = false;
	tx_chunk = TX_CHUNK_SIZE;
	memset(&stream, 0, sizeof(stream));
	aa_frame_unlock();
}

void aa_frame_set_encrypted(bool on)
{
	aa_frame_lock();
	encrypted = on;
	if (on) {
		size_t max_rec = aa_tls_max_record_payload();

		tx_chunk = (max_rec > 0U) ? MIN(TX_CHUNK_SIZE, max_rec) : TX_CHUNK_SIZE;
	}
	aa_frame_unlock();
}

static size_t put_header(uint8_t *hdr, uint8_t channel, uint8_t flags, uint16_t len,
			 bool has_total, uint32_t total)
{
	hdr[0] = channel;
	hdr[1] = flags;
	sys_put_be16(len, &hdr[2]);
	if (has_total) {
		sys_put_be32(total, &hdr[4]);
		return AA_FRAME_MAX_HDR;
	}

	return AA_FRAME_HDR_LEN;
}

int aa_frame_tx_record(const uint8_t *record, size_t len)
{
	const struct aa_transport *t = aa_transport_get();
	size_t hdr_len;
	size_t payload_len;
	uint8_t *p;

	if (!encrypted) {
		/* Handshake record inside an SSL_HANDSHAKE control message */
		payload_len = 2U + len;
		if (payload_len > sizeof(tx_frame) - AA_FRAME_HDR_LEN) {
			return -EMSGSIZE;
		}
		hdr_len = put_header(tx_frame, AA_CHANNEL_CONTROL, AA_FRAME_BULK,
				     (uint16_t)payload_len, false, 0U);
		p = &tx_frame[hdr_len];
		sys_put_be16(AA_CTRL_SSL_HANDSHAKE, p);
		memcpy(p + 2U, record, len);
	} else {
		payload_len = len;
		if (payload_len > sizeof(tx_frame) - AA_FRAME_MAX_HDR) {
			return -EMSGSIZE;
		}
		hdr_len = put_header(tx_frame, tx_hdr.channel, tx_hdr.flags | AA_FRAME_ENCRYPTED,
				     (uint16_t)payload_len, tx_hdr.has_total, tx_hdr.total);
		memcpy(&tx_frame[hdr_len], record, len);
	}

	return t->write(tx_frame, hdr_len + payload_len);
}

/* Send one plaintext chunk with the given framing; lock held */
static int send_chunk(uint8_t channel, uint8_t flags, bool has_total, uint32_t total,
		      const uint8_t *plain, size_t len)
{
	const struct aa_transport *t = aa_transport_get();
	size_t hdr_len;

	if (encrypted) {
		tx_hdr.channel = channel;
		tx_hdr.flags = flags;
		tx_hdr.has_total = has_total;
		tx_hdr.total = total;
		return aa_tls_write(plain, len);
	}

	if (len > sizeof(tx_frame) - AA_FRAME_MAX_HDR) {
		return -EMSGSIZE;
	}

	hdr_len = put_header(tx_frame, channel, flags, (uint16_t)len, has_total, total);
	memcpy(&tx_frame[hdr_len], plain, len);

	return t->write(tx_frame, hdr_len + len);
}

int aa_msg_send(uint8_t channel, bool control, uint16_t msg_id, const uint8_t *body, size_t len)
{
	uint8_t flags = AA_FRAME_BULK | (control ? AA_FRAME_CONTROL : 0U);
	int ret;

	if (len + 2U > tx_chunk) {
		return -EMSGSIZE;
	}

	aa_frame_lock();
	sys_put_be16(msg_id, tx_plain);
	if (len > 0U) {
		memcpy(&tx_plain[2], body, len);
	}
	ret = send_chunk(channel, flags, false, 0U, tx_plain, len + 2U);
	aa_frame_unlock();

	return ret;
}

int aa_msg_begin(uint8_t channel, uint16_t msg_id, size_t body_len)
{
	aa_frame_lock();
	if (stream.active) {
		aa_frame_unlock();
		return -EBUSY;
	}

	stream.active = true;
	stream.channel = channel;
	stream.total = 2U + body_len;
	stream.written = 0U;
	stream.first_sent = false;
	sys_put_be16(msg_id, tx_plain);
	stream.plain_len = 2U;
	aa_frame_unlock();

	return 0;
}

/* Flush the staged plaintext as one fragment; lock held */
static int stream_flush(bool last)
{
	uint8_t flags = 0U;
	bool has_total = false;
	int ret;

	if (!stream.first_sent) {
		flags |= AA_FRAME_FIRST;
		has_total = !last;
	}
	if (last) {
		flags |= AA_FRAME_LAST;
	}

	ret = send_chunk(stream.channel, flags, has_total, (uint32_t)stream.total, tx_plain,
			 stream.plain_len);
	stream.written += stream.plain_len;
	stream.plain_len = 0U;
	stream.first_sent = true;

	return ret;
}

int aa_msg_write(const uint8_t *data, size_t len)
{
	size_t off = 0;
	int ret = 0;

	aa_frame_lock();
	if (!stream.active) {
		aa_frame_unlock();
		return -EINVAL;
	}

	if (stream.written + stream.plain_len + len > stream.total) {
		aa_frame_unlock();
		return -EMSGSIZE;
	}

	while (off < len) {
		size_t room = tx_chunk - stream.plain_len;
		size_t n = MIN(room, len - off);

		memcpy(&tx_plain[stream.plain_len], data + off, n);
		stream.plain_len += n;
		off += n;

		if (stream.plain_len == tx_chunk &&
		    stream.written + stream.plain_len < stream.total) {
			ret = stream_flush(false);
			if (ret != 0) {
				break;
			}
		}
	}
	aa_frame_unlock();

	return ret;
}

int aa_msg_end(void)
{
	int ret;

	aa_frame_lock();
	if (!stream.active) {
		aa_frame_unlock();
		return -EINVAL;
	}

	if (stream.written + stream.plain_len != stream.total) {
		LOG_ERR("Streamed message is %zu bytes, %zu announced",
			stream.written + stream.plain_len, stream.total);
		memset(&stream, 0, sizeof(stream));
		aa_frame_unlock();
		return -EMSGSIZE;
	}

	ret = stream_flush(true);
	memset(&stream, 0, sizeof(stream));
	aa_frame_unlock();

	return ret;
}

void aa_msg_abort(void)
{
	aa_frame_lock();
	memset(&stream, 0, sizeof(stream));
	aa_frame_unlock();
}

static int read_exact(uint8_t *buf, size_t len, k_timeout_t timeout)
{
	const struct aa_transport *t = aa_transport_get();
	size_t off = 0;

	while (off < len) {
		int n = t->read(buf + off, len - off, timeout);

		if (n == -ETIMEDOUT && off == 0U) {
			return -ETIMEDOUT;
		}
		if (n < 0 && n != -ETIMEDOUT) {
			return n;
		}
		if (n > 0) {
			off += (size_t)n;
		}
		timeout = K_FOREVER;
	}

	return 0;
}

int aa_frame_recv(struct aa_frame_rx *frame, k_timeout_t timeout)
{
	uint8_t hdr[AA_FRAME_MAX_HDR];
	int ret;

	ret = read_exact(hdr, AA_FRAME_HDR_LEN, timeout);
	if (ret != 0) {
		return ret;
	}

	frame->channel = hdr[0];
	frame->flags = hdr[1];
	frame->len = sys_get_be16(&hdr[2]);
	frame->total = 0U;

	if ((frame->flags & AA_FRAME_BULK) == AA_FRAME_FIRST) {
		ret = read_exact(&hdr[4], AA_FRAME_TOTAL_LEN, K_FOREVER);
		if (ret != 0) {
			return ret;
		}
		frame->total = sys_get_be32(&hdr[4]);
	}

	if (frame->len > sizeof(rx_frame)) {
		LOG_ERR("Frame of %u bytes exceeds the RX buffer", frame->len);
		return -EMSGSIZE;
	}

	ret = read_exact(rx_frame, frame->len, K_FOREVER);
	if (ret != 0) {
		return ret;
	}

	frame->payload = rx_frame;

	return 0;
}
