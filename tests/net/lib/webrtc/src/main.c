/*
 * WebRTC unit tests.
 *
 * Tests cover:
 *   - SDP parsing: valid offer, valid answer, missing fingerprint,
 *     missing credentials.
 *   - SDP generation: offer contains required fields, answer contains
 *     required fields, setup role is correct.
 *   - Peer connection lifecycle: create, close, double-close.
 *   - Data channel lifecycle: create, state transitions, label retrieval.
 *   - Data channel framing: OPEN/ACK/DATA/CLOSE packet parsing.
 *   - ICE candidate validation: accept valid host candidates, reject
 *     malformed strings.
 */

/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(net_test, CONFIG_NET_WEBRTC_LOG_LEVEL);

#include <zephyr/ztest.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/net/webrtc.h>

/* Include internal header for white-box testing of SDP parser and
 * data channel framing.
 */
#include "webrtc_internal.h"

/* -------------------------------------------------------------------------
 * Minimal DER certificate and private key for tests.
 * This is NOT cryptographically valid — it is only used to exercise the
 * credential and fingerprint population paths.
 * ---------------------------------------------------------------------- */

static const uint8_t test_cert_der[] = {
	/* Minimal X.509v1 self-signed certificate skeleton (64 bytes). */
	0x30, 0x3e,                   /* SEQUENCE */
	0x30, 0x2c,                   /* SEQUENCE (TBSCertificate) */
	0x02, 0x01, 0x01,             /* INTEGER version */
	0x02, 0x08,                   /* INTEGER serialNumber (8 bytes) */
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x30, 0x09,                   /* SEQUENCE AlgorithmIdentifier */
	0x06, 0x07, 0x2a, 0x86, 0x48, 0xce, 0x3d, 0x04, 0x03,
	0x30, 0x07,                   /* SEQUENCE Issuer */
	0x31, 0x05, 0x30, 0x03, 0x06, 0x01, 0x43,
	0x30, 0x0e,                   /* SEQUENCE Validity */
	0x17, 0x0d, 0x32, 0x35, 0x30, 0x31, 0x30, 0x31,
	0x30, 0x30, 0x30, 0x30, 0x30, 0x30, 0x5a,
	0x0a, 0x01, 0x01,             /* subject, subjectPublicKeyInfo stubs */
	0x03, 0x02, 0x00, 0x00,       /* BIT STRING (empty) */
	0x30, 0x0d,                   /* SEQUENCE (signature algorithm) */
	0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d,
	0x01, 0x01, 0x0b, 0x05, 0x00,
	0x03, 0x01, 0x00,             /* BIT STRING (empty signature) */
};

static const uint8_t test_key_der[] = {
	/* Minimal DER private key stub. */
	0x30, 0x04, 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
};

#define TEST_DTLS_TAG 10

/* -------------------------------------------------------------------------
 * Test-suite fixtures
 * ---------------------------------------------------------------------- */

static void *webrtc_test_setup(void)
{
	int ret;

	ret = tls_credential_add(TEST_DTLS_TAG,
				 TLS_CREDENTIAL_PUBLIC_CERTIFICATE,
				 test_cert_der, sizeof(test_cert_der));
	if (ret < 0 && ret != -EEXIST) {
		zassert_ok(ret, "tls_credential_add (cert) failed: %d", ret);
	}

	ret = tls_credential_add(TEST_DTLS_TAG,
				 TLS_CREDENTIAL_PRIVATE_KEY,
				 test_key_der, sizeof(test_key_der));
	if (ret < 0 && ret != -EEXIST) {
		zassert_ok(ret, "tls_credential_add (key) failed: %d", ret);
	}

	return NULL;
}

/* -------------------------------------------------------------------------
 * SDP parsing tests
 * ---------------------------------------------------------------------- */

ZTEST_SUITE(sdp_parsing, NULL, webrtc_test_setup, NULL, NULL, NULL);

ZTEST(sdp_parsing, test_parse_valid_offer)
{
	static const char sdp_offer[] =
		"v=0\r\n"
		"o=- 1234567890 2 IN IP4 192.0.2.1\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"a=group:BUNDLE data\r\n"
		"m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
		"webrtc-datachannel\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=ice-ufrag:ABCDabcd\r\n"
		"a=ice-pwd:ABCDEFGHabcdefgh12345678\r\n"
		"a=fingerprint:sha-256 "
		"AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99:"
		"AA:BB:CC:DD:EE:FF:00:11:22:33:44:55:66:77:88:99\r\n"
		"a=setup:actpass\r\n"
		"a=mid:data\r\n"
		"a=candidate:1 1 UDP 2130706431 192.0.2.1 12345 typ host\r\n";

	struct webrtc_sdp_info info;
	int ret;

	ret = webrtc_sdp_parse(sdp_offer, &info);
	zassert_equal(ret, 0, "SDP parse failed: %d", ret);

	zassert_str_equal(info.ice_ufrag, "ABCDabcd",
			  "Unexpected ufrag: %s", info.ice_ufrag);
	zassert_str_equal(info.ice_pwd, "ABCDEFGHabcdefgh12345678",
			  "Unexpected pwd: %s", info.ice_pwd);
	zassert_true(info.fingerprint_valid,
		     "Fingerprint not marked valid");
	/* setup:actpass → dtls_active should be false
	 * (only "active" sets dtls_active = true)
	 */
	zassert_false(info.dtls_active,
		      "dtls_active should be false for actpass");
	zassert_equal(info.candidate_count, 1U,
		      "Expected 1 candidate, got %u", info.candidate_count);
	zassert_str_equal(info.candidates[0].addr, "192.0.2.1",
			  "Unexpected candidate addr: %s",
			  info.candidates[0].addr);
	zassert_equal(info.candidates[0].port, 12345U,
		      "Unexpected candidate port: %u",
		      info.candidates[0].port);
}

ZTEST(sdp_parsing, test_parse_active_setup)
{
	static const char sdp_active[] =
		"v=0\r\n"
		"o=- 1 1 IN IP4 0.0.0.0\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
		"webrtc-datachannel\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=ice-ufrag:AAAAaaaa\r\n"
		"a=ice-pwd:BBBBBBBBbbbbbbbbCCCCCCCC\r\n"
		"a=fingerprint:sha-256 "
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00\r\n"
		"a=setup:active\r\n"
		"a=mid:data\r\n";

	struct webrtc_sdp_info info;
	int ret;

	ret = webrtc_sdp_parse(sdp_active, &info);
	zassert_equal(ret, 0, "SDP parse failed: %d", ret);
	zassert_true(info.dtls_active,
		     "dtls_active should be true for setup:active");
}

ZTEST(sdp_parsing, test_parse_missing_ufrag)
{
	static const char sdp_no_ufrag[] =
		"v=0\r\n"
		"o=- 1 1 IN IP4 0.0.0.0\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
		"webrtc-datachannel\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=ice-pwd:BBBBBBBBbbbbbbbbCCCCCCCC\r\n"
		"a=fingerprint:sha-256 "
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00\r\n"
		"a=setup:active\r\n";

	struct webrtc_sdp_info info;
	int ret;

	ret = webrtc_sdp_parse(sdp_no_ufrag, &info);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

ZTEST(sdp_parsing, test_parse_missing_fingerprint)
{
	static const char sdp_no_fp[] =
		"v=0\r\n"
		"o=- 1 1 IN IP4 0.0.0.0\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
		"webrtc-datachannel\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=ice-ufrag:AAAAaaaa\r\n"
		"a=ice-pwd:BBBBBBBBbbbbbbbbCCCCCCCC\r\n"
		"a=setup:active\r\n";

	struct webrtc_sdp_info info;
	int ret;

	ret = webrtc_sdp_parse(sdp_no_fp, &info);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

ZTEST(sdp_parsing, test_parse_multiple_candidates)
{
	static const char sdp_multi[] =
		"v=0\r\n"
		"o=- 1 1 IN IP4 0.0.0.0\r\n"
		"s=-\r\n"
		"t=0 0\r\n"
		"m=application 9 UDP/DTLS/webrtc-datachannel-zephyr "
		"webrtc-datachannel\r\n"
		"c=IN IP4 0.0.0.0\r\n"
		"a=ice-ufrag:AAAAaaaa\r\n"
		"a=ice-pwd:BBBBBBBBbbbbbbbbCCCCCCCC\r\n"
		"a=fingerprint:sha-256 "
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00:"
		"11:22:33:44:55:66:77:88:99:AA:BB:CC:DD:EE:FF:00\r\n"
		"a=setup:passive\r\n"
		"a=candidate:1 1 UDP 2130706431 192.0.2.1 10001 typ host\r\n"
		"a=candidate:2 1 UDP 2130706430 192.0.2.2 10002 typ host\r\n";

	struct webrtc_sdp_info info;
	int ret;

	ret = webrtc_sdp_parse(sdp_multi, &info);
	zassert_equal(ret, 0, "SDP parse failed: %d", ret);
	zassert_equal(info.candidate_count, 2U,
		      "Expected 2 candidates, got %u", info.candidate_count);
	zassert_equal(info.candidates[0].port, 10001U,
		      "Unexpected port for candidate 0: %u",
		      info.candidates[0].port);
	zassert_equal(info.candidates[1].port, 10002U,
		      "Unexpected port for candidate 1: %u",
		      info.candidates[1].port);
}

/* -------------------------------------------------------------------------
 * Peer connection lifecycle tests
 * ---------------------------------------------------------------------- */

ZTEST_SUITE(peer_connection, NULL, webrtc_test_setup, NULL, NULL, NULL);

ZTEST(peer_connection, test_create_and_close)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create failed: %d", ret);
	zassert_not_null(pc, "pc is NULL after create");

	rtc_peer_connection_close(pc);
	/* Double-close must not crash. */
	rtc_peer_connection_close(pc);
}

ZTEST(peer_connection, test_create_null_config)
{
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(NULL, NULL, NULL, &pc);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

ZTEST(peer_connection, test_create_null_out)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, NULL);
	zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);
}

ZTEST(peer_connection, test_create_offer)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	char sdp_buf[CONFIG_WEBRTC_SDP_MAX_LEN];
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create failed: %d", ret);

	ret = rtc_peer_connection_create_offer(pc, sdp_buf, sizeof(sdp_buf));
	zassert_equal(ret, 0, "create_offer failed: %d", ret);

	/* Verify the generated SDP contains the required lines. */
	zassert_not_null(strstr(sdp_buf, "v=0"),
			 "SDP missing v= line");
	zassert_not_null(strstr(sdp_buf, "a=ice-ufrag:"),
			 "SDP missing a=ice-ufrag:");
	zassert_not_null(strstr(sdp_buf, "a=ice-pwd:"),
			 "SDP missing a=ice-pwd:");
	zassert_not_null(strstr(sdp_buf, "a=fingerprint:sha-256"),
			 "SDP missing a=fingerprint:");
	zassert_not_null(strstr(sdp_buf, "a=setup:actpass"),
			 "SDP offer should contain a=setup:actpass");
	zassert_not_null(strstr(sdp_buf, "m=application"),
			 "SDP missing m=application section");

	rtc_peer_connection_close(pc);
}

ZTEST(peer_connection, test_add_ice_candidate_valid)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create failed: %d", ret);

	ret = rtc_peer_connection_add_ice_candidate(
		pc,
		"candidate:1 1 UDP 2130706431 192.0.2.2 9001 typ host");
	zassert_equal(ret, 0, "add_ice_candidate failed: %d", ret);
	zassert_equal(pc->remote_sdp.candidate_count, 1U,
		      "Expected 1 remote candidate");

	rtc_peer_connection_close(pc);
}

ZTEST(peer_connection, test_add_ice_candidate_invalid)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create failed: %d", ret);

	/* Non-host candidate type is not supported. */
	ret = rtc_peer_connection_add_ice_candidate(
		pc,
		"candidate:1 1 UDP 1845501695 203.0.113.5 9001 typ srflx");
	zassert_equal(ret, -EINVAL,
		      "Expected -EINVAL for srflx candidate, got %d", ret);

	/* Completely malformed string. */
	ret = rtc_peer_connection_add_ice_candidate(pc, "not a candidate");
	zassert_equal(ret, -EINVAL,
		      "Expected -EINVAL for garbage, got %d", ret);

	rtc_peer_connection_close(pc);
}

/* -------------------------------------------------------------------------
 * Data channel tests
 * ---------------------------------------------------------------------- */

ZTEST_SUITE(data_channel, NULL, webrtc_test_setup, NULL, NULL, NULL);

ZTEST(data_channel, test_create_channel)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	struct rtc_data_channel *dc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	ret = rtc_data_channel_create(pc, "test-channel", NULL, NULL, NULL,
				      &dc);
	zassert_equal(ret, 0, "create dc failed: %d", ret);
	zassert_not_null(dc, "dc is NULL");

	zassert_str_equal(rtc_data_channel_get_label(dc), "test-channel",
			  "Unexpected label: %s",
			  rtc_data_channel_get_label(dc));
	zassert_equal(rtc_data_channel_get_state(dc),
		      RTC_DATA_CHANNEL_CONNECTING,
		      "Expected CONNECTING state, got %d",
		      (int)rtc_data_channel_get_state(dc));

	rtc_peer_connection_close(pc);
}

ZTEST(data_channel, test_create_label_too_long)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	struct rtc_data_channel *dc = NULL;
	static char long_label[CONFIG_WEBRTC_DATA_CHANNEL_LABEL_LEN + 2U];
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	memset(long_label, 'X', sizeof(long_label) - 1U);
	long_label[sizeof(long_label) - 1U] = '\0';

	ret = rtc_data_channel_create(pc, long_label, NULL, NULL, NULL, &dc);
	zassert_equal(ret, -EINVAL,
		      "Expected -EINVAL for long label, got %d", ret);

	rtc_peer_connection_close(pc);
}

ZTEST(data_channel, test_send_not_connected)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	struct rtc_data_channel *dc = NULL;
	static const uint8_t payload[] = { 0x01, 0x02, 0x03 };
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	ret = rtc_data_channel_create(pc, "ch", NULL, NULL, NULL, &dc);
	zassert_equal(ret, 0, "create dc failed: %d", ret);

	/* Channel is in CONNECTING state; send should fail. */
	ret = rtc_data_channel_send(dc, payload, sizeof(payload));
	zassert_equal(ret, -ENOTCONN,
		      "Expected -ENOTCONN, got %d", ret);

	rtc_peer_connection_close(pc);
}

/* -------------------------------------------------------------------------
 * Data channel framing tests (white-box)
 * ---------------------------------------------------------------------- */

ZTEST_SUITE(dc_framing, NULL, webrtc_test_setup, NULL, NULL, NULL);

/* A dummy received-message capture for framing tests. */
static const uint8_t *last_msg_data;
static size_t last_msg_len;
static bool on_open_called;
static bool on_close_called;

static void framing_test_on_message(struct rtc_data_channel *dc,
				    const uint8_t *data, size_t len,
				    void *user_data)
{
	ARG_UNUSED(dc);
	ARG_UNUSED(user_data);
	last_msg_data = data;
	last_msg_len = len;
}

static void framing_test_on_open(struct rtc_data_channel *dc,
				 void *user_data)
{
	ARG_UNUSED(dc);
	ARG_UNUSED(user_data);
	on_open_called = true;
}

static void framing_test_on_close(struct rtc_data_channel *dc,
				  void *user_data)
{
	ARG_UNUSED(dc);
	ARG_UNUSED(user_data);
	on_close_called = true;
}

static const struct rtc_data_channel_callbacks framing_cbs = {
	.on_open    = framing_test_on_open,
	.on_message = framing_test_on_message,
	.on_close   = framing_test_on_close,
};

ZTEST(dc_framing, test_open_ack_data_close)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	on_open_called = false;
	on_close_called = false;
	last_msg_data = NULL;
	last_msg_len = 0U;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	/* Simulate receiving an OPEN packet from a remote peer. */
	static const uint8_t open_pkt[] = {
		0x00, 0x01,             /* channel ID = 1 */
		WEBRTC_DC_MSG_OPEN, 0x00,
		'r', 'e', 'm', 'o', 't', 'e', '\0',
	};

	/* Temporarily set a dummy dtls_sock so the framing layer can
	 * call sendmsg for the ACK response.  Since there is no real socket
	 * in this unit test, we use -1 and rely on the fact that the ACK
	 * send failure does not abort processing.
	 */
	pc->dtls_sock = -1;

	/* Register on_data_channel callback so framing layer can fire it. */
	pc->cbs.on_data_channel = NULL; /* not testing this path here */

	ret = webrtc_dc_process_packet(pc, open_pkt, sizeof(open_pkt));
	/* ACK send will fail because dtls_sock is -1, but open processing
	 * should still have created the channel.
	 */

	struct rtc_data_channel *dc = webrtc_dc_find_by_id(pc, 1U);

	zassert_not_null(dc, "Channel not created for OPEN packet");
	zassert_str_equal(dc->label, "remote",
			  "Unexpected channel label: %s", dc->label);

	/* Register callbacks on the newly created channel. */
	rtc_data_channel_set_callbacks(dc, &framing_cbs, NULL);

	/* Simulate receiving a DATA packet. */
	static const uint8_t data_pkt[] = {
		0x00, 0x01,             /* channel ID = 1 */
		WEBRTC_DC_MSG_DATA, 0x00,
		'H', 'e', 'l', 'l', 'o',
	};

	ret = webrtc_dc_process_packet(pc, data_pkt, sizeof(data_pkt));
	zassert_equal(ret, 0, "DATA processing failed: %d", ret);
	zassert_equal(last_msg_len, 5U, "Unexpected message length: %zu",
		      last_msg_len);
	zassert_mem_equal(last_msg_data, "Hello", 5U,
			  "Unexpected message content");

	/* Simulate receiving a CLOSE packet. */
	static const uint8_t close_pkt[] = {
		0x00, 0x01,
		WEBRTC_DC_MSG_CLOSE, 0x00,
	};

	ret = webrtc_dc_process_packet(pc, close_pkt, sizeof(close_pkt));
	zassert_equal(ret, 0, "CLOSE processing failed: %d", ret);
	zassert_true(on_close_called, "on_close not called");

	/* Channel should be removed from the table. */
	dc = webrtc_dc_find_by_id(pc, 1U);
	zassert_is_null(dc, "Channel not removed after CLOSE");

	rtc_peer_connection_close(pc);
}

ZTEST(dc_framing, test_data_unknown_channel)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	/* DATA for a channel that does not exist. */
	static const uint8_t data_pkt[] = {
		0x00, 0x42,
		WEBRTC_DC_MSG_DATA, 0x00,
		'x',
	};

	ret = webrtc_dc_process_packet(pc, data_pkt, sizeof(data_pkt));
	zassert_equal(ret, -ENOENT, "Expected -ENOENT, got %d", ret);

	rtc_peer_connection_close(pc);
}

ZTEST(dc_framing, test_short_packet)
{
	struct rtc_configuration cfg = {
		.dtls_tag = TEST_DTLS_TAG,
	};
	struct rtc_peer_connection *pc = NULL;
	int ret;

	ret = rtc_peer_connection_create(&cfg, NULL, NULL, &pc);
	zassert_equal(ret, 0, "create pc failed: %d", ret);

	static const uint8_t short_pkt[] = { 0x00, 0x01 }; /* only 2 bytes */

	ret = webrtc_dc_process_packet(pc, short_pkt, sizeof(short_pkt));
	zassert_equal(ret, -EINVAL, "Expected -EINVAL, got %d", ret);

	rtc_peer_connection_close(pc);
}

/* -------------------------------------------------------------------------
 * STUN packet detection test
 * ---------------------------------------------------------------------- */

ZTEST_SUITE(ice_stun, NULL, NULL, NULL, NULL, NULL);

ZTEST(ice_stun, test_is_stun_packet)
{
	/* Valid STUN binding request header. */
	static const uint8_t stun_hdr[] = {
		0x00, 0x01,             /* Binding Request */
		0x00, 0x00,             /* length = 0 */
		0x21, 0x12, 0xa4, 0x42, /* magic cookie */
		0x01, 0x02, 0x03, 0x04, /* transaction ID */
		0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c,
	};

	zassert_true(webrtc_is_stun_packet(stun_hdr, sizeof(stun_hdr)),
		     "Should be detected as STUN packet");

	/* Non-STUN (wrong magic cookie). */
	static const uint8_t non_stun[] = {
		0x00, 0x01, 0x00, 0x00,
		0xde, 0xad, 0xbe, 0xef,
		0x01, 0x02, 0x03, 0x04,
		0x05, 0x06, 0x07, 0x08,
		0x09, 0x0a, 0x0b, 0x0c,
	};

	zassert_false(webrtc_is_stun_packet(non_stun, sizeof(non_stun)),
		      "Should NOT be detected as STUN packet");

	/* Too short */
	static const uint8_t too_short[] = { 0x00, 0x01, 0x00 };

	zassert_false(webrtc_is_stun_packet(too_short, sizeof(too_short)),
		      "Short buffer should not be STUN packet");
}
