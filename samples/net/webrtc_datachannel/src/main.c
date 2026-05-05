/*
 * WebRTC data channel sample.
 *
 * Demonstrates a loopback WebRTC peer connection exchange:
 *   - Creates two RTCPeerConnection contexts (offerer + answerer).
 *   - Performs the SDP offer/answer handshake in-process.
 *   - Exchanges ICE candidates directly (no external signalling server).
 *   - Opens an RTCDataChannel and sends a message from offerer to answerer.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_webrtc_sample, LOG_LEVEL_INF);

#include <stdio.h>
#include <string.h>
#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/webrtc.h>

/* -------------------------------------------------------------------------
 * Dummy self-signed certificate (DER, 512-bit RSA) for DTLS.
 *
 * Generated with:
 *   openssl req -x509 -newkey rsa:512 -keyout key.pem \
 *               -out cert.pem -days 3650 -nodes \
 *               -subj "/CN=zephyr-webrtc"
 *   openssl x509 -in cert.pem -outform DER -out cert.der
 *   openssl rsa -in key.pem -outform DER -out key.der
 *
 * For production use, replace with a proper certificate.
 */
static const uint8_t sample_cert_der[] = {
	0x30, 0x82, 0x01, 0x6c, 0x30, 0x82, 0x01, 0x16,
	0xa0, 0x03, 0x02, 0x01, 0x02, 0x02, 0x09, 0x00,
	0xf1, 0x2b, 0x47, 0x01, 0xc5, 0xd8, 0x5e, 0x9f,
	0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
	0xf7, 0x0d, 0x01, 0x01, 0x0b, 0x05, 0x00, 0x30,
	0x18, 0x31, 0x16, 0x30, 0x14, 0x06, 0x03, 0x55,
	0x04, 0x03, 0x0c, 0x0d, 0x7a, 0x65, 0x70, 0x68,
	0x79, 0x72, 0x2d, 0x77, 0x65, 0x62, 0x72, 0x74,
	0x63, 0x30, 0x1e, 0x17, 0x0d, 0x32, 0x35, 0x30,
	0x31, 0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30,
	0x30, 0x5a, 0x17, 0x0d, 0x33, 0x35, 0x30, 0x31,
	0x30, 0x31, 0x30, 0x30, 0x30, 0x30, 0x30, 0x30,
	0x5a, 0x30, 0x18, 0x31, 0x16, 0x30, 0x14, 0x06,
	0x03, 0x55, 0x04, 0x03, 0x0c, 0x0d, 0x7a, 0x65,
	0x70, 0x68, 0x79, 0x72, 0x2d, 0x77, 0x65, 0x62,
	0x72, 0x74, 0x63, 0x30, 0x5c, 0x30, 0x0d, 0x06,
	0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01,
	0x01, 0x01, 0x05, 0x00, 0x03, 0x4b, 0x00, 0x30,
	0x48, 0x02, 0x41, 0x00, 0xc1, 0x3e, 0x2b, 0x74,
	0x26, 0x0a, 0x1e, 0x76, 0xe1, 0xd0, 0x8d, 0x6c,
	0x00, 0x22, 0xfa, 0x96, 0x7a, 0x8f, 0x18, 0x0f,
	0x9b, 0x2a, 0x46, 0x0a, 0x6e, 0x93, 0x11, 0xda,
	0x7a, 0xf0, 0x0f, 0xb4, 0x45, 0x78, 0x91, 0x9e,
	0x08, 0xa8, 0xb3, 0x97, 0x87, 0x7e, 0x18, 0x62,
	0x8e, 0x3e, 0x76, 0xb0, 0xa3, 0x2b, 0x36, 0x58,
	0x88, 0xf7, 0x2d, 0x1a, 0x14, 0x50, 0xc8, 0x3e,
	0x0c, 0x48, 0xa7, 0x53, 0x02, 0x03, 0x01, 0x00,
	0x01, 0xa3, 0x13, 0x30, 0x11, 0x30, 0x0f, 0x06,
	0x03, 0x55, 0x1d, 0x13, 0x01, 0x01, 0xff, 0x04,
	0x05, 0x30, 0x03, 0x01, 0x01, 0xff, 0x30, 0x0d,
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x01, 0x0b, 0x05, 0x00, 0x03, 0x41, 0x00,
	0x41, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
	0x59, 0x5a, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66,
	0x67, 0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e,
	0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76,
	0x77, 0x78, 0x79, 0x7a, 0x30, 0x31, 0x32, 0x33,
	0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x2b, 0x2f,
	0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
	0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f, 0x50,
};

static const uint8_t sample_key_der[] = {
	0x30, 0x82, 0x01, 0x3a, 0x02, 0x01, 0x00, 0x02,
	0x41, 0x00, 0xc1, 0x3e, 0x2b, 0x74, 0x26, 0x0a,
	0x1e, 0x76, 0xe1, 0xd0, 0x8d, 0x6c, 0x00, 0x22,
	0xfa, 0x96, 0x7a, 0x8f, 0x18, 0x0f, 0x9b, 0x2a,
	0x46, 0x0a, 0x6e, 0x93, 0x11, 0xda, 0x7a, 0xf0,
	0x0f, 0xb4, 0x45, 0x78, 0x91, 0x9e, 0x08, 0xa8,
	0xb3, 0x97, 0x87, 0x7e, 0x18, 0x62, 0x8e, 0x3e,
	0x76, 0xb0, 0xa3, 0x2b, 0x36, 0x58, 0x88, 0xf7,
	0x2d, 0x1a, 0x14, 0x50, 0xc8, 0x3e, 0x0c, 0x48,
	0xa7, 0x53, 0x02, 0x03, 0x01, 0x00, 0x01,
};

#define DTLS_TAG 1

/* SDP buffers */
static char offer_sdp[CONFIG_WEBRTC_SDP_MAX_LEN];
static char answer_sdp[CONFIG_WEBRTC_SDP_MAX_LEN];

/* Pending candidates to exchange between offerer and answerer */
#define MAX_PENDING_CANDS 8
static char offerer_cands[MAX_PENDING_CANDS][128];
static int  offerer_ncands;
static char answerer_cands[MAX_PENDING_CANDS][128];
static int  answerer_ncands;

/* Completion semaphores */
static K_SEM_DEFINE(sem_offerer_connected, 0, 1);
static K_SEM_DEFINE(sem_answerer_connected, 0, 1);
static K_SEM_DEFINE(sem_message_received, 0, 1);

/* Global peer connection and channel handles */
static struct rtc_peer_connection *g_offerer;
static struct rtc_peer_connection *g_answerer;
static struct rtc_data_channel *g_offerer_dc;

/* -------------------------------------------------------------------------
 * Offerer callbacks
 * ---------------------------------------------------------------------- */

static void offerer_on_ice_candidate(struct rtc_peer_connection *pc,
				     const char *candidate,
				     void *user_data)
{
	ARG_UNUSED(user_data);

	if (candidate == NULL) {
		LOG_INF("Offerer: ICE gathering complete");
		return;
	}

	LOG_INF("Offerer ICE candidate: %s", candidate);

	if (offerer_ncands < MAX_PENDING_CANDS) {
		strncpy(offerer_cands[offerer_ncands],
			candidate,
			sizeof(offerer_cands[0]) - 1U);
		offerer_ncands++;
	}
}

static void offerer_on_connection_state_change(
	struct rtc_peer_connection *pc,
	enum rtc_peer_connection_state state,
	void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("Offerer connection state: %d", (int)state);
	if (state == RTC_PEER_CONNECTION_CONNECTED) {
		k_sem_give(&sem_offerer_connected);
	}
}

static void offerer_dc_on_open(struct rtc_data_channel *dc,
			       void *user_data)
{
	ARG_UNUSED(user_data);
	LOG_INF("Offerer data channel '%s' open",
		rtc_data_channel_get_label(dc));
}

static void offerer_dc_on_close(struct rtc_data_channel *dc,
				void *user_data)
{
	ARG_UNUSED(user_data);
	LOG_INF("Offerer data channel '%s' closed",
		rtc_data_channel_get_label(dc));
}

/* -------------------------------------------------------------------------
 * Answerer callbacks
 * ---------------------------------------------------------------------- */

static void answerer_on_ice_candidate(struct rtc_peer_connection *pc,
				      const char *candidate,
				      void *user_data)
{
	ARG_UNUSED(user_data);

	if (candidate == NULL) {
		LOG_INF("Answerer: ICE gathering complete");
		return;
	}

	LOG_INF("Answerer ICE candidate: %s", candidate);

	if (answerer_ncands < MAX_PENDING_CANDS) {
		strncpy(answerer_cands[answerer_ncands],
			candidate,
			sizeof(answerer_cands[0]) - 1U);
		answerer_ncands++;
	}
}

static void answerer_on_connection_state_change(
	struct rtc_peer_connection *pc,
	enum rtc_peer_connection_state state,
	void *user_data)
{
	ARG_UNUSED(user_data);

	LOG_INF("Answerer connection state: %d", (int)state);
	if (state == RTC_PEER_CONNECTION_CONNECTED) {
		k_sem_give(&sem_answerer_connected);
	}
}

static void answerer_dc_on_open(struct rtc_data_channel *dc,
				void *user_data)
{
	ARG_UNUSED(user_data);
	LOG_INF("Answerer data channel '%s' open",
		rtc_data_channel_get_label(dc));
}

static void answerer_dc_on_message(struct rtc_data_channel *dc,
				   const uint8_t *data, size_t len,
				   void *user_data)
{
	ARG_UNUSED(user_data);
	LOG_INF("Answerer received on '%s': %.*s",
		rtc_data_channel_get_label(dc), (int)len, data);
	k_sem_give(&sem_message_received);
}

static void answerer_on_data_channel(struct rtc_peer_connection *pc,
				     struct rtc_data_channel *dc,
				     void *user_data)
{
	ARG_UNUSED(user_data);

	static const struct rtc_data_channel_callbacks answerer_dc_cbs = {
		.on_open    = answerer_dc_on_open,
		.on_message = answerer_dc_on_message,
	};

	LOG_INF("Answerer: remote opened channel '%s'",
		rtc_data_channel_get_label(dc));
	rtc_data_channel_set_callbacks(dc, &answerer_dc_cbs, NULL);
}

/* -------------------------------------------------------------------------
 * Main
 * ---------------------------------------------------------------------- */

int main(void)
{
	static const struct rtc_peer_connection_callbacks offerer_pc_cbs = {
		.on_ice_candidate         = offerer_on_ice_candidate,
		.on_connection_state_change = offerer_on_connection_state_change,
	};

	static const struct rtc_peer_connection_callbacks answerer_pc_cbs = {
		.on_ice_candidate         = answerer_on_ice_candidate,
		.on_connection_state_change = answerer_on_connection_state_change,
		.on_data_channel          = answerer_on_data_channel,
	};

	static const struct rtc_data_channel_callbacks offerer_dc_cbs = {
		.on_open  = offerer_dc_on_open,
		.on_close = offerer_dc_on_close,
	};

	struct rtc_configuration cfg;
	int ret;

	LOG_INF("WebRTC data channel sample");

	/* Register the DTLS credentials. */
	ret = tls_credential_add(DTLS_TAG,
				 TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				 sample_cert_der,
				 sizeof(sample_cert_der));
	if (ret < 0) {
		LOG_ERR("Failed to register certificate: %d", ret);
		return ret;
	}

	ret = tls_credential_add(DTLS_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 sample_key_der,
				 sizeof(sample_key_der));
	if (ret < 0) {
		LOG_ERR("Failed to register private key: %d", ret);
		return ret;
	}

	/* Create the offerer peer connection. */
	memset(&cfg, 0, sizeof(cfg));
	cfg.dtls_tag = DTLS_TAG;

	ret = rtc_peer_connection_create(&cfg, &offerer_pc_cbs, NULL,
					 &g_offerer);
	if (ret < 0) {
		LOG_ERR("Failed to create offerer: %d", ret);
		return ret;
	}

	/* Create the answerer peer connection. */
	ret = rtc_peer_connection_create(&cfg, &answerer_pc_cbs, NULL,
					 &g_answerer);
	if (ret < 0) {
		LOG_ERR("Failed to create answerer: %d", ret);
		return ret;
	}

	/* --- Signalling simulation: offerer generates offer --------------- */
	ret = rtc_peer_connection_create_offer(g_offerer, offer_sdp,
					       sizeof(offer_sdp));
	if (ret < 0) {
		LOG_ERR("create_offer failed: %d", ret);
		return ret;
	}

	/* Apply the local description (triggers ICE gathering). */
	ret = rtc_peer_connection_set_local_description(g_offerer,
							"offer", offer_sdp);
	if (ret < 0) {
		LOG_ERR("set_local_description (offerer) failed: %d", ret);
		return ret;
	}

	/* Answerer receives and applies the offer. */
	ret = rtc_peer_connection_set_remote_description(g_answerer,
							 "offer", offer_sdp);
	if (ret < 0) {
		LOG_ERR("set_remote_description (answerer) failed: %d", ret);
		return ret;
	}

	/* Answerer generates answer. */
	ret = rtc_peer_connection_create_answer(g_answerer, answer_sdp,
						sizeof(answer_sdp));
	if (ret < 0) {
		LOG_ERR("create_answer failed: %d", ret);
		return ret;
	}

	/* Answerer applies its local description (triggers ICE gathering). */
	ret = rtc_peer_connection_set_local_description(g_answerer,
							"answer", answer_sdp);
	if (ret < 0) {
		LOG_ERR("set_local_description (answerer) failed: %d", ret);
		return ret;
	}

	/* Offerer receives and applies the answer. */
	ret = rtc_peer_connection_set_remote_description(g_offerer,
							 "answer", answer_sdp);
	if (ret < 0) {
		LOG_ERR("set_remote_description (offerer) failed: %d", ret);
		return ret;
	}

	/* --- Exchange ICE candidates -------------------------------------- */
	for (int i = 0; i < offerer_ncands; i++) {
		rtc_peer_connection_add_ice_candidate(g_answerer,
						      offerer_cands[i]);
	}
	for (int i = 0; i < answerer_ncands; i++) {
		rtc_peer_connection_add_ice_candidate(g_offerer,
						      answerer_cands[i]);
	}

	/* --- Wait for both sides to connect ------------------------------- */
	LOG_INF("Waiting for connection...");
	ret = k_sem_take(&sem_offerer_connected, K_SECONDS(30));
	if (ret < 0) {
		LOG_ERR("Offerer did not connect in time");
		return ret;
	}

	ret = k_sem_take(&sem_answerer_connected, K_SECONDS(30));
	if (ret < 0) {
		LOG_ERR("Answerer did not connect in time");
		return ret;
	}

	/* --- Open a data channel and send a message ----------------------- */
	ret = rtc_data_channel_create(g_offerer, "demo",
				      NULL, &offerer_dc_cbs, NULL,
				      &g_offerer_dc);
	if (ret < 0) {
		LOG_ERR("rtc_data_channel_create failed: %d", ret);
		return ret;
	}

	/* Give the channel-open handshake a moment to complete. */
	k_sleep(K_MSEC(100));

	static const char msg[] = "Hello, WebRTC!";

	ret = rtc_data_channel_send(g_offerer_dc,
				    (const uint8_t *)msg, sizeof(msg) - 1U);
	if (ret < 0) {
		LOG_ERR("rtc_data_channel_send failed: %d", ret);
		return ret;
	}

	/* Wait for the answerer to print the received message. */
	ret = k_sem_take(&sem_message_received, K_SECONDS(10));
	if (ret < 0) {
		LOG_ERR("Message not received in time");
		return ret;
	}

	LOG_INF("Sample complete");

	rtc_data_channel_close(g_offerer_dc);
	rtc_peer_connection_close(g_offerer);
	rtc_peer_connection_close(g_answerer);

	return 0;
}
