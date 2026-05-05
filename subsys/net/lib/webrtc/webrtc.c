/*
 * WebRTC peer connection: public API implementation and state machine.
 *
 * Ties together ICE candidate gathering, STUN connectivity checks, DTLS
 * transport setup, and data channel management.
 *
 * State transitions:
 *
 *   NEW
 *    │  set_local_description(offer)
 *    ▼
 *   HAVE_LOCAL_OFFER  ──── set_remote_description(answer) ──►  GATHERING
 *    ▲                                                              │
 *    │  set_remote_description(offer)                    ICE done  │
 *    │                                                              ▼
 *   HAVE_REMOTE_OFFER ─── set_local_description(answer)  ──►   CHECKING
 *    │                                                              │
 *    │                                                    DTLS done │
 *    │                                                              ▼
 *    │                                                          CONNECTED
 *    └──────────────────────────────────────────────────────────────┘
 *
 *   CONNECTED / CHECKING  ──── error / close() ──►  FAILED / CLOSED
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_webrtc, CONFIG_NET_WEBRTC_LOG_LEVEL);

#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/socket.h>
#include <zephyr/random/random.h>

#include <psa/crypto.h>

#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * Global context pool
 * ---------------------------------------------------------------------- */

static struct rtc_peer_connection
	g_pc_pool[CONFIG_WEBRTC_MAX_PEER_CONNECTIONS];

static K_MUTEX_DEFINE(g_pool_lock);

/* -------------------------------------------------------------------------
 * Receive buffer for the I/O work handler
 * ---------------------------------------------------------------------- */

#define IO_RECV_BUF_SIZE CONFIG_WEBRTC_IO_RECV_BUF_SIZE

static uint8_t io_recv_buf[IO_RECV_BUF_SIZE];

/* -------------------------------------------------------------------------
 * State helpers
 * ---------------------------------------------------------------------- */

void webrtc_pc_set_connection_state(struct rtc_peer_connection *pc,
				    enum rtc_peer_connection_state state)
{
	if (pc->cbs.on_connection_state_change != NULL) {
		pc->cbs.on_connection_state_change(pc, state, pc->user_data);
	}
}

void webrtc_pc_set_gathering_state(struct rtc_peer_connection *pc,
				   enum rtc_ice_gathering_state state)
{
	if (pc->cbs.on_gathering_state_change != NULL) {
		pc->cbs.on_gathering_state_change(pc, state, pc->user_data);
	}
}

/**
 * Notify the application of each gathered ICE candidate via the
 * on_ice_candidate callback.
 */
static void notify_ice_candidates(struct rtc_peer_connection *pc)
{
	if (pc->cbs.on_ice_candidate == NULL) {
		return;
	}

	for (uint8_t i = 0U; i < pc->local_candidate_count; i++) {
		const struct webrtc_ice_candidate *c =
			&pc->local_candidates[i];
		char cand_str[WEBRTC_CANDIDATE_STR_LEN + 1U];

		if (!c->valid) {
			continue;
		}

		snprintk(cand_str, sizeof(cand_str),
			 "candidate:%u %u UDP %u %s %u typ host",
			 (unsigned int)i + 1U,
			 (unsigned int)c->component,
			 (unsigned int)c->priority,
			 c->addr,
			 (unsigned int)c->port);

		pc->cbs.on_ice_candidate(pc, cand_str, pc->user_data);
	}

	/* NULL candidate signals gathering complete (W3C behaviour). */
	pc->cbs.on_ice_candidate(pc, NULL, pc->user_data);
}

/* -------------------------------------------------------------------------
 * I/O work handler
 *
 * Drives ICE connectivity checks and receives data on the DTLS socket.
 * Scheduled to run periodically while the connection is alive.
 * ---------------------------------------------------------------------- */

void webrtc_pc_io_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork =
		CONTAINER_OF(work, struct k_work_delayable, work);
	struct rtc_peer_connection *pc =
		CONTAINER_OF(dwork, struct rtc_peer_connection, io_work);
	int ret;

	k_mutex_lock(&pc->lock, K_FOREVER);

	if (pc->int_state == WEBRTC_PC_INT_CLOSED ||
	    pc->int_state == WEBRTC_PC_INT_NEW) {
		k_mutex_unlock(&pc->lock);
		return;
	}

	/* --- ICE phase: send connectivity checks ------------------------- */
	if (pc->int_state == WEBRTC_PC_INT_CHECKING) {
		if (pc->selected_remote_idx < 0) {
			/* Continue sending checks. */
			webrtc_ice_start_checks(pc);

			/* Try to receive a STUN response. */
			if (pc->ice_sock >= 0) {
				struct sockaddr from;
				socklen_t from_len = sizeof(from);
				ssize_t n;

				n = zsock_recvfrom(
					pc->ice_sock,
					io_recv_buf, sizeof(io_recv_buf),
					ZSOCK_MSG_DONTWAIT,
					&from, &from_len);
				if (n > 0 &&
				    webrtc_is_stun_packet(io_recv_buf,
							  (size_t)n)) {
					webrtc_ice_process_packet(
						pc, io_recv_buf,
						(size_t)n, &from);
				}
			}

			/* Check if ICE now has a nominated pair. */
			if (pc->selected_remote_idx >= 0) {
				LOG_INF("WebRTC: ICE connected, starting DTLS");
				ret = webrtc_dtls_connect(pc);
				if (ret < 0) {
					LOG_ERR("WebRTC: DTLS connect failed: %d",
						ret);
					pc->int_state = WEBRTC_PC_INT_FAILED;
					webrtc_pc_set_connection_state(
						pc,
						RTC_PEER_CONNECTION_FAILED);
					k_mutex_unlock(&pc->lock);
					return;
				}

				pc->int_state = WEBRTC_PC_INT_CONNECTED;
				webrtc_pc_set_connection_state(
					pc, RTC_PEER_CONNECTION_CONNECTED);
				LOG_INF("WebRTC: connection established");
			}
		}
	}

	/* --- Connected phase: receive data from DTLS --------------------- */
	if (pc->int_state == WEBRTC_PC_INT_CONNECTED &&
	    pc->dtls_sock >= 0) {
		ssize_t n;

		n = zsock_recv(pc->dtls_sock, io_recv_buf,
			       sizeof(io_recv_buf), ZSOCK_MSG_DONTWAIT);
		if (n > 0) {
			webrtc_dc_process_packet(pc, io_recv_buf, (size_t)n);
		} else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
			LOG_ERR("WebRTC: DTLS recv error: %d", errno);
			pc->int_state = WEBRTC_PC_INT_FAILED;
			webrtc_pc_set_connection_state(
				pc, RTC_PEER_CONNECTION_FAILED);
			k_mutex_unlock(&pc->lock);
			return;
		}
	}

	k_mutex_unlock(&pc->lock);

	/* Reschedule if still active. */
	if (pc->int_state != WEBRTC_PC_INT_CLOSED &&
	    pc->int_state != WEBRTC_PC_INT_FAILED) {
		k_work_reschedule(&pc->io_work,
				  K_MSEC(CONFIG_WEBRTC_IO_POLL_INTERVAL_MS));
	}
}

/* -------------------------------------------------------------------------
 * ICE credential generation
 * ---------------------------------------------------------------------- */

/**
 * Fill @p sdp with randomly generated ICE credentials.
 */
static void generate_ice_credentials(struct webrtc_sdp_info *sdp)
{
	static const char charset[] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
		"0123456789+/";
	uint8_t raw[WEBRTC_ICE_PWD_LEN];
	size_t i;

	/* ufrag */
	sys_rand_get(raw, WEBRTC_ICE_UFRAG_LEN);
	for (i = 0U; i < WEBRTC_ICE_UFRAG_LEN; i++) {
		sdp->ice_ufrag[i] =
			charset[raw[i] % (sizeof(charset) - 1U)];
	}
	sdp->ice_ufrag[WEBRTC_ICE_UFRAG_LEN] = '\0';

	/* pwd */
	sys_rand_get(raw, WEBRTC_ICE_PWD_LEN);
	for (i = 0U; i < WEBRTC_ICE_PWD_LEN; i++) {
		sdp->ice_pwd[i] =
			charset[raw[i] % (sizeof(charset) - 1U)];
	}
	sdp->ice_pwd[WEBRTC_ICE_PWD_LEN] = '\0';
}

/**
 * Populate the DTLS certificate fingerprint in @p sdp from the credential
 * registered under @p dtls_tag.
 *
 * The credential must be of type TLS_CREDENTIAL_PUBLIC_CERTIFICATE and
 * must be a DER-encoded X.509 certificate.  We compute SHA-256 over the
 * raw DER bytes (which is equivalent to the fingerprint of the certificate's
 * public key material as used in WebRTC SDP).
 */
static int populate_fingerprint(struct webrtc_sdp_info *sdp,
				sec_tag_t dtls_tag)
{
	const void *cert = NULL;
	size_t cert_len = 0U;
	psa_status_t status;
	size_t hash_len;
	int ret;

	ret = tls_credential_get(dtls_tag,
				 TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				 &cert, &cert_len);
	if (ret != 0) {
		LOG_ERR("WebRTC: cannot get certificate for tag %d: %d",
			(int)dtls_tag, ret);
		return ret;
	}

	status = psa_hash_compute(PSA_ALG_SHA_256,
				  (const uint8_t *)cert, cert_len,
				  sdp->fingerprint,
				  WEBRTC_FINGERPRINT_LEN,
				  &hash_len);
	if (status != PSA_SUCCESS || hash_len != WEBRTC_FINGERPRINT_LEN) {
		LOG_ERR("WebRTC: SHA-256 of certificate failed: %d",
			(int)status);
		return -EIO;
	}

	sdp->fingerprint_valid = true;
	return 0;
}

/* -------------------------------------------------------------------------
 * rtc_peer_connection_create
 * ---------------------------------------------------------------------- */

int rtc_peer_connection_create(const struct rtc_configuration *config,
			       const struct rtc_peer_connection_callbacks *cbs,
			       void *user_data,
			       struct rtc_peer_connection **pc_out)
{
	struct rtc_peer_connection *pc = NULL;
	int ret;

	if (config == NULL || pc_out == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&g_pool_lock, K_FOREVER);
	for (int i = 0; i < CONFIG_WEBRTC_MAX_PEER_CONNECTIONS; i++) {
		if (atomic_get(&g_pc_pool[i].refcount) == 0) {
			pc = &g_pc_pool[i];
			atomic_set(&pc->refcount, 1);
			break;
		}
	}
	k_mutex_unlock(&g_pool_lock);

	if (pc == NULL) {
		LOG_ERR("WebRTC: no free peer connection context");
		return -ENOMEM;
	}

	memset(pc, 0, sizeof(*pc));
	atomic_set(&pc->refcount, 1);
	k_mutex_init(&pc->lock);

	pc->config = *config;
	if (cbs != NULL) {
		pc->cbs = *cbs;
	}
	pc->user_data = user_data;
	pc->int_state = WEBRTC_PC_INT_NEW;
	pc->ice_sock = -1;
	pc->dtls_sock = -1;
	pc->selected_local_idx = -1;
	pc->selected_remote_idx = -1;
	pc->next_channel_id = 0U;

	k_work_init_delayable(&pc->io_work, webrtc_pc_io_work_handler);

	ret = populate_fingerprint(&pc->local_sdp, config->dtls_tag);
	if (ret != 0) {
		atomic_set(&pc->refcount, 0);
		return ret;
	}

	generate_ice_credentials(&pc->local_sdp);

	*pc_out = pc;
	LOG_INF("WebRTC: peer connection created");
	return 0;
}

/* -------------------------------------------------------------------------
 * rtc_peer_connection_close
 * ---------------------------------------------------------------------- */

void rtc_peer_connection_close(struct rtc_peer_connection *pc)
{
	if (pc == NULL) {
		return;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	pc->int_state = WEBRTC_PC_INT_CLOSED;

	webrtc_dc_close_all(pc);
	webrtc_dtls_close(pc);

	if (pc->ice_sock >= 0) {
		zsock_close(pc->ice_sock);
		pc->ice_sock = -1;
	}

	k_work_cancel_delayable(&pc->io_work);
	webrtc_pc_set_connection_state(pc, RTC_PEER_CONNECTION_CLOSED);

	k_mutex_unlock(&pc->lock);
	atomic_set(&pc->refcount, 0);
	LOG_INF("WebRTC: peer connection closed");
}

/* -------------------------------------------------------------------------
 * Offer / Answer
 * ---------------------------------------------------------------------- */

int rtc_peer_connection_create_offer(struct rtc_peer_connection *pc,
				     char *sdp_buf, size_t sdp_len)
{
	int ret;

	if (pc == NULL || sdp_buf == NULL || sdp_len == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	if (pc->int_state != WEBRTC_PC_INT_NEW &&
	    pc->int_state != WEBRTC_PC_INT_HAVE_REMOTE_OFFER) {
		k_mutex_unlock(&pc->lock);
		return -EINVAL;
	}

	pc->is_offerer = true;
	pc->local_sdp.dtls_active = false; /* offerer defaults to actpass */

	ret = webrtc_sdp_generate_offer(pc, sdp_buf, sdp_len);
	if (ret == 0) {
		pc->int_state = WEBRTC_PC_INT_HAVE_LOCAL_OFFER;
	}

	k_mutex_unlock(&pc->lock);
	return ret;
}

int rtc_peer_connection_create_answer(struct rtc_peer_connection *pc,
				      char *sdp_buf, size_t sdp_len)
{
	int ret;

	if (pc == NULL || sdp_buf == NULL || sdp_len == 0U) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	if (!pc->have_remote_desc) {
		LOG_ERR("WebRTC: no remote offer; cannot create answer");
		k_mutex_unlock(&pc->lock);
		return -EINVAL;
	}

	pc->is_offerer = false;

	/* Answerer becomes DTLS active unless the remote chose "active". */
	pc->local_sdp.dtls_active = !pc->remote_sdp.dtls_active;

	ret = webrtc_sdp_generate_answer(pc, sdp_buf, sdp_len);

	k_mutex_unlock(&pc->lock);
	return ret;
}

/* -------------------------------------------------------------------------
 * set_local_description
 * ---------------------------------------------------------------------- */

int rtc_peer_connection_set_local_description(struct rtc_peer_connection *pc,
					      const char *type,
					      const char *sdp)
{
	int ret;

	if (pc == NULL || type == NULL || sdp == NULL) {
		return -EINVAL;
	}

	if (strcmp(type, "offer") != 0 && strcmp(type, "answer") != 0) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	/* Trigger ICE gathering. */
	webrtc_pc_set_gathering_state(pc, RTC_ICE_GATHERING_GATHERING);
	ret = webrtc_ice_gather_candidates(pc);
	if (ret != 0) {
		LOG_ERR("WebRTC: ICE gathering failed: %d", ret);
		pc->int_state = WEBRTC_PC_INT_FAILED;
		webrtc_pc_set_connection_state(pc, RTC_PEER_CONNECTION_FAILED);
		k_mutex_unlock(&pc->lock);
		return ret;
	}

	webrtc_pc_set_gathering_state(pc, RTC_ICE_GATHERING_COMPLETE);
	notify_ice_candidates(pc);

	if (strcmp(type, "offer") == 0) {
		pc->int_state = WEBRTC_PC_INT_HAVE_LOCAL_OFFER;
	} else {
		/* Answer: remote offer was already set → start checks. */
		if (pc->have_remote_desc) {
			pc->int_state = WEBRTC_PC_INT_CHECKING;
			webrtc_pc_set_connection_state(
				pc, RTC_PEER_CONNECTION_CONNECTING);
			k_work_reschedule(
				&pc->io_work,
				K_MSEC(CONFIG_WEBRTC_IO_POLL_INTERVAL_MS));
		}
	}

	k_mutex_unlock(&pc->lock);
	return 0;
}

/* -------------------------------------------------------------------------
 * set_remote_description
 * ---------------------------------------------------------------------- */

int rtc_peer_connection_set_remote_description(struct rtc_peer_connection *pc,
					       const char *type,
					       const char *sdp)
{
	int ret;

	if (pc == NULL || type == NULL || sdp == NULL) {
		return -EINVAL;
	}

	if (strcmp(type, "offer") != 0 && strcmp(type, "answer") != 0) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	ret = webrtc_sdp_parse(sdp, &pc->remote_sdp);
	if (ret != 0) {
		LOG_ERR("WebRTC: failed to parse remote SDP: %d", ret);
		k_mutex_unlock(&pc->lock);
		return ret;
	}

	pc->have_remote_desc = true;

	if (strcmp(type, "offer") == 0) {
		pc->is_offerer = false;
		pc->int_state = WEBRTC_PC_INT_HAVE_REMOTE_OFFER;
	} else {
		/* We are the offerer and received the answer: start checks. */
		pc->int_state = WEBRTC_PC_INT_CHECKING;
		webrtc_pc_set_connection_state(
			pc, RTC_PEER_CONNECTION_CONNECTING);
		k_work_reschedule(
			&pc->io_work,
			K_MSEC(CONFIG_WEBRTC_IO_POLL_INTERVAL_MS));
	}

	k_mutex_unlock(&pc->lock);
	return 0;
}

/* -------------------------------------------------------------------------
 * add_ice_candidate
 * ---------------------------------------------------------------------- */

int rtc_peer_connection_add_ice_candidate(struct rtc_peer_connection *pc,
					  const char *candidate)
{
	struct webrtc_ice_candidate *c;
	unsigned int foundation, component, priority, port;
	char addr[INET6_ADDRSTRLEN];
	char typ[16];

	if (pc == NULL || candidate == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	if (pc->remote_sdp.candidate_count >= WEBRTC_MAX_ICE_CANDIDATES) {
		k_mutex_unlock(&pc->lock);
		return -ENOSPC;
	}

	if (sscanf(candidate,
		   "candidate:%u %u UDP %u %39s %u typ %15s",
		   &foundation, &component, &priority,
		   addr, &port, typ) != 6 ||
	    strcmp(typ, "host") != 0) {
		LOG_WRN("WebRTC: unsupported or malformed candidate: %s",
			candidate);
		k_mutex_unlock(&pc->lock);
		return -EINVAL;
	}

	c = &pc->remote_sdp.candidates[pc->remote_sdp.candidate_count];
	strncpy(c->addr, addr, sizeof(c->addr) - 1U);
	c->port = (uint16_t)port;
	c->component = (uint8_t)component;
	c->priority = priority;
	c->valid = true;
	pc->remote_sdp.candidate_count++;

	LOG_DBG("WebRTC: added remote candidate %s:%u", c->addr, c->port);

	k_mutex_unlock(&pc->lock);
	return 0;
}

/* -------------------------------------------------------------------------
 * Data channel API
 * ---------------------------------------------------------------------- */

int rtc_data_channel_create(struct rtc_peer_connection *pc,
			    const char *label,
			    const struct rtc_data_channel_init *init,
			    const struct rtc_data_channel_callbacks *cbs,
			    void *user_data,
			    struct rtc_data_channel **dc_out)
{
	struct rtc_data_channel *dc;
	int slot = -1;

	if (pc == NULL || label == NULL || dc_out == NULL) {
		return -EINVAL;
	}

	if (strlen(label) > CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN) {
		return -EINVAL;
	}

	k_mutex_lock(&pc->lock, K_FOREVER);

	for (int i = 0; i < CONFIG_WEBRTC_MAX_DATA_CHANNELS; i++) {
		if (pc->channels[i] == NULL) {
			slot = i;
			break;
		}
	}

	if (slot < 0) {
		LOG_ERR("WebRTC: no free data channel slot");
		k_mutex_unlock(&pc->lock);
		return -ENOMEM;
	}

	dc = k_malloc(sizeof(*dc));
	if (dc == NULL) {
		k_mutex_unlock(&pc->lock);
		return -ENOMEM;
	}

	memset(dc, 0, sizeof(*dc));
	dc->pc = pc;
	dc->id = pc->next_channel_id++;
	dc->state = RTC_DATA_CHANNEL_CONNECTING;
	strncpy(dc->label, label, sizeof(dc->label) - 1U);

	if (init != NULL) {
		dc->ordered = init->ordered;
	} else {
		dc->ordered = true;
	}

	if (cbs != NULL) {
		dc->cbs = *cbs;
	}
	dc->user_data = user_data;

	pc->channels[slot] = dc;
	*dc_out = dc;

	k_mutex_unlock(&pc->lock);

	/* If already connected, send OPEN immediately. */
	if (pc->int_state == WEBRTC_PC_INT_CONNECTED && pc->dtls_sock >= 0) {
		webrtc_dc_send_open(pc, dc);
	}

	LOG_INF("WebRTC: data channel '%s' created (id=%u)", label, dc->id);
	return 0;
}

void rtc_data_channel_set_callbacks(struct rtc_data_channel *dc,
				    const struct rtc_data_channel_callbacks *cbs,
				    void *user_data)
{
	if (dc == NULL) {
		return;
	}

	k_mutex_lock(&dc->pc->lock, K_FOREVER);
	if (cbs != NULL) {
		dc->cbs = *cbs;
	} else {
		memset(&dc->cbs, 0, sizeof(dc->cbs));
	}
	dc->user_data = user_data;
	k_mutex_unlock(&dc->pc->lock);
}

int rtc_data_channel_send(struct rtc_data_channel *dc,
			  const uint8_t *data, size_t len)
{
	struct rtc_peer_connection *pc;
	uint8_t hdr[WEBRTC_DC_HEADER_LEN];
	struct iovec iov[2];
	struct msghdr mh;
	int ret;

	if (dc == NULL || data == NULL) {
		return -EINVAL;
	}

	if (dc->state != RTC_DATA_CHANNEL_OPEN) {
		return -ENOTCONN;
	}

	pc = dc->pc;

	if (pc->dtls_sock < 0) {
		return -ENOTCONN;
	}

	hdr[0] = (uint8_t)(dc->id >> 8U);
	hdr[1] = (uint8_t)(dc->id & 0xFFU);
	hdr[2] = WEBRTC_DC_MSG_DATA;
	hdr[3] = 0U;

	iov[0].iov_base = hdr;
	iov[0].iov_len = WEBRTC_DC_HEADER_LEN;
	iov[1].iov_base = (void *)data;
	iov[1].iov_len = len;

	memset(&mh, 0, sizeof(mh));
	mh.msg_iov = iov;
	mh.msg_iovlen = 2;

	ret = zsock_sendmsg(pc->dtls_sock, &mh, 0);
	if (ret < 0) {
		LOG_ERR("WebRTC DC send: %d", errno);
		return -errno;
	}

	/* Return payload bytes sent (subtract the header). */
	return MAX(0, ret - (int)WEBRTC_DC_HEADER_LEN);
}

void rtc_data_channel_close(struct rtc_data_channel *dc)
{
	struct rtc_peer_connection *pc;

	if (dc == NULL) {
		return;
	}

	pc = dc->pc;
	k_mutex_lock(&pc->lock, K_FOREVER);

	if (dc->state == RTC_DATA_CHANNEL_OPEN ||
	    dc->state == RTC_DATA_CHANNEL_CONNECTING) {
		if (pc->dtls_sock >= 0) {
			uint8_t hdr[WEBRTC_DC_HEADER_LEN];

			hdr[0] = (uint8_t)(dc->id >> 8U);
			hdr[1] = (uint8_t)(dc->id & 0xFFU);
			hdr[2] = WEBRTC_DC_MSG_CLOSE;
			hdr[3] = 0U;
			zsock_send(pc->dtls_sock, hdr, sizeof(hdr), 0);
		}
		dc->state = RTC_DATA_CHANNEL_CLOSING;
	}

	k_mutex_unlock(&pc->lock);
}

const char *rtc_data_channel_get_label(const struct rtc_data_channel *dc)
{
	return (dc != NULL) ? dc->label : NULL;
}

enum rtc_data_channel_state rtc_data_channel_get_state(
	const struct rtc_data_channel *dc)
{
	return (dc != NULL) ? dc->state : RTC_DATA_CHANNEL_CLOSED;
}

/* -------------------------------------------------------------------------
 * Module initialisation
 * ---------------------------------------------------------------------- */

void webrtc_init(void)
{
	LOG_DBG("WebRTC: module initialised");
}
