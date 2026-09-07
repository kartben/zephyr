/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_session.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include "src/aa.pb.h"
#include "aa_control.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_input.h"
#include "aa_sensor.h"
#include "aa_tls.h"
#include "aa_transport.h"
#include "aa_video.h"
#include "ui.h"

LOG_MODULE_REGISTER(aa_session, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Receive thread: waits for a head unit on the transport, parses frames,
 * decrypts and reassembles messages and dispatches them to the channel
 * handlers. A session that does not reach the running state within
 * CONFIG_SAMPLE_AA_SESSION_TIMEOUT_MS is torn down so that the head unit
 * re-enumerates the device.
 */

#define RX_POLL_MS 500

static struct aa_session session;
static bool session_failed;
/* Uptime of the first message from the head unit, 0 while the link is idle */
static int64_t session_start_ms;

static uint8_t rx_msg[CONFIG_SAMPLE_AA_RX_MSG_SIZE];
static struct {
	bool active;
	uint8_t channel;
	uint8_t flags;
	size_t len;
	uint32_t expected;
} reassembly;

static K_THREAD_STACK_DEFINE(rx_stack, CONFIG_SAMPLE_AA_RX_STACK_SIZE);
static struct k_thread rx_thread_data;

static const char *const state_names[] = {
	[AA_STATE_LINK_DOWN] = "link down",
	[AA_STATE_WAIT_VERSION] = "wait version",
	[AA_STATE_HANDSHAKE] = "handshake",
	[AA_STATE_WAIT_AUTH] = "wait auth",
	[AA_STATE_WAIT_SERVICE_DISCOVERY] = "wait service discovery",
	[AA_STATE_OPENING_CHANNELS] = "opening channels",
	[AA_STATE_VIDEO_SETUP] = "video setup",
	[AA_STATE_RUNNING] = "running",
	[AA_STATE_SHUTDOWN] = "shutdown",
};

struct aa_session *aa_session_get(void)
{
	return &session;
}

void aa_session_set_state(enum aa_state state)
{
	if (state != session.state) {
		LOG_INF("State: %s -> %s", state_names[session.state], state_names[state]);
		session.state = state;
	}
}

void aa_session_abort(const char *reason)
{
	if (!session_failed) {
		LOG_WRN("Session aborted: %s", reason);
		session_failed = true;
	}
}

int aa_pb_encode(uint8_t *buf, size_t size, const pb_msgdesc_t *fields, const void *msg)
{
	pb_ostream_t stream = pb_ostream_from_buffer(buf, size);

	if (!pb_encode(&stream, fields, msg)) {
		LOG_ERR("Encoding failed: %s", PB_GET_ERROR(&stream));
		return -EMSGSIZE;
	}

	return (int)stream.bytes_written;
}

int aa_pb_decode(const uint8_t *buf, size_t len, const pb_msgdesc_t *fields, void *msg)
{
	pb_istream_t stream = pb_istream_from_buffer(buf, len);

	if (!pb_decode(&stream, fields, msg)) {
		LOG_ERR("Decoding failed: %s", PB_GET_ERROR(&stream));
		return -EBADMSG;
	}

	return 0;
}

static int send_channel_open(uint8_t channel)
{
	ChannelOpenRequest req = ChannelOpenRequest_init_zero;
	uint8_t buf[16];
	int len;

	req.has_priority = true;
	req.priority = 0;
	req.has_channel_id = true;
	req.channel_id = channel;

	len = aa_pb_encode(buf, sizeof(buf), ChannelOpenRequest_fields, &req);
	if (len < 0) {
		return len;
	}

	LOG_INF("Opening channel %u", channel);

	return aa_msg_send(channel, true, AA_CTRL_CHANNEL_OPEN_REQUEST, buf, (size_t)len);
}

int aa_session_open_channels(void)
{
	session.open_count = 0U;
	session.open_idx = 0U;
	if (session.has_video) {
		session.open_list[session.open_count++] = session.video_ch;
	}
	if (session.has_input) {
		session.open_list[session.open_count++] = session.input_ch;
	}
	if (session.has_sensor) {
		session.open_list[session.open_count++] = session.sensor_ch;
	}

	if (session.open_count == 0U) {
		return -ENOENT;
	}

	aa_session_set_state(AA_STATE_OPENING_CHANNELS);

	return send_channel_open(session.open_list[0]);
}

static void on_channel_open_response(uint8_t channel, const uint8_t *body, size_t len)
{
	ChannelOpenResponse rsp = ChannelOpenResponse_init_zero;
	int ret;

	if (session.state != AA_STATE_OPENING_CHANNELS ||
	    channel != session.open_list[session.open_idx]) {
		LOG_WRN("Unexpected channel open response on channel %u", channel);
		return;
	}

	if (aa_pb_decode(body, len, ChannelOpenResponse_fields, &rsp) != 0 ||
	    (rsp.has_status && rsp.status != AA_STATUS_OK)) {
		aa_session_abort("channel open refused");
		return;
	}

	session.open_idx++;
	if (session.open_idx < session.open_count) {
		if (send_channel_open(session.open_list[session.open_idx]) != 0) {
			aa_session_abort("channel open request failed");
		}
		return;
	}

	aa_session_set_state(AA_STATE_VIDEO_SETUP);
	ret = aa_video_setup();
	if (ret != 0) {
		aa_session_abort("video setup request failed");
		return;
	}
	if (session.has_input) {
		(void)aa_input_bind();
	}
	if (session.has_sensor) {
		(void)aa_sensor_start();
	}
}

static void deliver(uint8_t channel, bool control, const uint8_t *payload, size_t len)
{
	uint16_t msg_id;
	const uint8_t *body;
	size_t body_len;

	if (len < 2U) {
		LOG_WRN("Short message on channel %u", channel);
		return;
	}

	msg_id = sys_get_be16(payload);
	body = payload + 2U;
	body_len = len - 2U;

	LOG_DBG("RX ch %u%s msg 0x%04x len %zu", channel, control ? " (control)" : "", msg_id,
		body_len);

	if (channel == AA_CHANNEL_CONTROL) {
		aa_control_handle(msg_id, body, body_len);
	} else if (msg_id == AA_CTRL_CHANNEL_OPEN_RESPONSE) {
		on_channel_open_response(channel, body, body_len);
	} else if (session.has_video && channel == session.video_ch) {
		aa_video_handle(msg_id, body, body_len);
	} else if (session.has_input && channel == session.input_ch) {
		aa_input_handle(msg_id, body, body_len);
	} else if (session.has_sensor && channel == session.sensor_ch) {
		aa_sensor_handle(msg_id, body, body_len);
	} else {
		LOG_WRN("Unhandled message 0x%04x on channel %u (%zu bytes)", msg_id, channel,
			body_len);
		LOG_HEXDUMP_DBG(body, MIN(body_len, 64U), "body");
	}
}

static void handle_frame(struct aa_frame_rx *frame)
{
	uint8_t *payload = frame->payload;
	size_t len = frame->len;
	uint8_t fragment = frame->flags & AA_FRAME_BULK;
	bool control = (frame->flags & AA_FRAME_CONTROL) != 0U;

	if ((frame->flags & AA_FRAME_ENCRYPTED) != 0U) {
		int n;

		if (!aa_tls_handshake_done()) {
			aa_session_abort("encrypted frame before the handshake");
			return;
		}

		n = aa_tls_decrypt(payload, len, CONFIG_SAMPLE_AA_RX_FRAME_SIZE);
		if (n < 0) {
			aa_session_abort("decryption failed");
			return;
		}
		len = (size_t)n;
	}

	if (fragment == AA_FRAME_BULK) {
		deliver(frame->channel, control, payload, len);
		return;
	}

	if (fragment == AA_FRAME_FIRST) {
		reassembly.active = true;
		reassembly.channel = frame->channel;
		reassembly.flags = frame->flags;
		reassembly.len = 0U;
		reassembly.expected = frame->total;
	} else if (!reassembly.active || reassembly.channel != frame->channel) {
		aa_session_abort("fragment without a message");
		return;
	}

	if (reassembly.len + len > sizeof(rx_msg)) {
		aa_session_abort("message larger than the RX message buffer");
		return;
	}

	memcpy(&rx_msg[reassembly.len], payload, len);
	reassembly.len += len;

	if (fragment == AA_FRAME_LAST) {
		if (reassembly.expected != 0U && reassembly.expected != reassembly.len) {
			LOG_WRN("Reassembled %zu bytes, %u announced", reassembly.len,
				reassembly.expected);
		}
		reassembly.active = false;
		deliver(frame->channel, (reassembly.flags & AA_FRAME_CONTROL) != 0U, rx_msg,
			reassembly.len);
	}
}

void aa_session_restart(void)
{
	memset(&session, 0, sizeof(session));
	memset(&reassembly, 0, sizeof(reassembly));
	session_start_ms = 0;

	aa_frame_reset();
	(void)aa_tls_reset();
	aa_video_link_up();
	aa_input_link_up();
	aa_session_set_state(AA_STATE_WAIT_VERSION);
	aa_ui_set_link_state(AA_UI_LINK_CONNECTED);
}

static void link_up(void)
{
	session_failed = false;
	aa_session_restart();
	LOG_INF("Link up, waiting for the head unit version request");
}

static void link_down(void)
{
	aa_video_link_down();
	aa_input_link_down();
	aa_frame_reset();
	aa_session_set_state(AA_STATE_LINK_DOWN);
	aa_ui_set_link_state(AA_UI_LINK_DOWN);
}

/*
 * A USB link is up as soon as the host configures the device, which says
 * nothing about a head unit being present, so the setup timeout only runs once
 * the head unit has started talking.
 */
static void check_setup_timeout(void)
{
	if (session_start_ms == 0 || session.state == AA_STATE_RUNNING ||
	    session.state == AA_STATE_SHUTDOWN) {
		return;
	}

	if ((k_uptime_get() - session_start_ms) > CONFIG_SAMPLE_AA_SESSION_TIMEOUT_MS) {
		aa_session_abort("session setup timed out");
	}
}

static void rx_loop(void)
{
	struct aa_frame_rx frame;

	while (!session_failed) {
		int ret = aa_frame_recv(&frame, K_MSEC(RX_POLL_MS));

		if (ret == 0) {
			if (session_start_ms == 0) {
				session_start_ms = k_uptime_get();
			}
			handle_frame(&frame);
			continue;
		}

		if (ret == -ETIMEDOUT) {
			check_setup_timeout();
			continue;
		}

		if (ret == -ENOTCONN) {
			LOG_INF("Link lost");
		} else {
			LOG_ERR("Receive error (%d)", ret);
		}
		break;
	}
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	const struct aa_transport *t = aa_transport_get();
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = t->open();
	if (ret != 0) {
		LOG_ERR("Transport %s failed to open (%d)", t->name, ret);
		return;
	}

	LOG_INF("Android Auto sample ready (transport %s)", t->name);

	for (;;) {
		ret = t->wait_link(K_FOREVER);
		if (ret != 0) {
			k_sleep(K_MSEC(100));
			continue;
		}

		link_up();
		rx_loop();
		link_down();
		t->close();
	}
}

int aa_session_start(void)
{
	int ret;

	ret = aa_frame_init();
	if (ret != 0) {
		return ret;
	}

	ret = aa_tls_init();
	if (ret != 0) {
		return ret;
	}

	ret = aa_video_init();
	if (ret != 0) {
		return ret;
	}

	k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack), rx_thread,
			NULL, NULL, NULL, CONFIG_SAMPLE_AA_RX_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread_data, "aa_rx");

	return 0;
}
