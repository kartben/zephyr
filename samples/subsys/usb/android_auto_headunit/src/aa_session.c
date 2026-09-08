/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_session.h"

#include "aa_tls.h"

#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include <pb_decode.h>
#include <pb_encode.h>

#include "aa_control.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_input.h"
#include "aa_audio.h"
#include "aa_mem.h"
#include "aa_sensor.h"
#include "aa_transport.h"
#include "aa_video.h"

LOG_MODULE_REGISTER(aa_session, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Receive thread: connects to the phone, drives the version exchange and the
 * TLS handshake, answers service discovery and dispatches channel messages.
 */

#define RX_POLL_MS 500

/*
 * A phone that has just been plugged in enumerates before its projection
 * service has opened the accessory endpoint, and anything sent in between is
 * lost, so the opening request is repeated, once per poll, until it is
 * answered.
 */
#define VERSION_RETRY_LIMIT 20

static struct aa_hu_session session;
static bool session_failed;

static uint8_t rx_msg[CONFIG_SAMPLE_AA_HU_RX_MSG_SIZE] AA_HU_BIG_BUF;
static struct {
	bool active;
	uint8_t channel;
	uint8_t flags;
	size_t len;
} reassembly;

static K_THREAD_STACK_DEFINE(rx_stack, CONFIG_SAMPLE_AA_HU_RX_STACK_SIZE);
static struct k_thread rx_thread_data;

static const char *const state_names[] = {
	[AA_HU_LINK_DOWN] = "link down",
	[AA_HU_WAIT_VERSION] = "wait version",
	[AA_HU_HANDSHAKE] = "handshake",
	[AA_HU_WAIT_SERVICE_DISCOVERY] = "wait service discovery",
	[AA_HU_RUNNING] = "running",
	[AA_HU_SHUTDOWN] = "shutdown",
};

struct aa_hu_session *aa_hu_session_get(void)
{
	return &session;
}

void aa_hu_session_set_state(enum aa_hu_state state)
{
	if (state != session.state) {
		LOG_INF("State: %s -> %s", state_names[session.state], state_names[state]);
		session.state = state;
	}
}

void aa_hu_session_abort(const char *reason)
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

static void deliver(uint8_t channel, bool control, const uint8_t *payload, size_t len)
{
	uint16_t msg_id;

	if (len < 2U) {
		return;
	}

	msg_id = sys_get_be16(payload);
	LOG_DBG("RX ch %u%s msg 0x%04x len %zu", channel, control ? " (control)" : "", msg_id,
		len - 2U);

	if (channel == AA_CHANNEL_CONTROL) {
		aa_control_handle(msg_id, payload + 2U, len - 2U);
	} else if (msg_id == AA_CTRL_CHANNEL_OPEN_REQUEST) {
		aa_control_channel_open(channel, payload + 2U, len - 2U);
	} else if (channel == session.video_ch) {
		aa_video_handle(msg_id, payload + 2U, len - 2U);
	} else if (channel == session.input_ch) {
		aa_input_handle(msg_id, payload + 2U, len - 2U);
	} else if (channel == session.sensor_ch) {
		aa_sensor_handle(msg_id, payload + 2U, len - 2U);
	} else if (aa_audio_is_audio_channel(channel)) {
		aa_audio_handle(channel, msg_id, payload + 2U, len - 2U);
	} else {
		LOG_WRN("Message 0x%04x on unexpected channel %u", msg_id, channel);
	}
}

static void handle_frame(struct aa_frame_rx *frame)
{
	uint8_t *payload = frame->payload;
	size_t len = frame->len;
	uint8_t fragment = frame->flags & AA_FRAME_BULK;
	bool control = (frame->flags & AA_FRAME_CONTROL) != 0U;

	if ((frame->flags & AA_FRAME_ENCRYPTED) != 0U) {
		int n = aa_tls_decrypt(payload, len, CONFIG_SAMPLE_AA_HU_RX_FRAME_SIZE);

		if (n < 0) {
			aa_hu_session_abort("decryption failed");
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
	} else if (!reassembly.active || reassembly.channel != frame->channel) {
		aa_hu_session_abort("fragment without a message");
		return;
	}

	if (reassembly.len + len > sizeof(rx_msg)) {
		aa_hu_session_abort("message larger than the reassembly buffer");
		return;
	}

	memcpy(&rx_msg[reassembly.len], payload, len);
	reassembly.len += len;

	if (fragment == AA_FRAME_LAST) {
		reassembly.active = false;
		deliver(frame->channel, (reassembly.flags & AA_FRAME_CONTROL) != 0U, rx_msg,
			reassembly.len);
	}
}

static void link_up(void)
{
	memset(&session, 0, sizeof(session));
	memset(&reassembly, 0, sizeof(reassembly));
	session_failed = false;

	aa_frame_reset();
	(void)aa_tls_reset();
	aa_video_link_up();
	aa_input_link_up();
	aa_hu_session_set_state(AA_HU_WAIT_VERSION);

	/* The head unit starts the conversation with a version request */
	if (aa_control_send_version_request() != 0) {
		aa_hu_session_abort("version request failed");
	}
}

static void rx_loop(void)
{
	struct aa_frame_rx frame;
	uint32_t version_tries = 1;

	while (!session_failed) {
		int ret = aa_frame_recv(&frame, K_MSEC(RX_POLL_MS));

		if (ret == 0) {
			handle_frame(&frame);
			continue;
		}

		if (ret != -ETIMEDOUT) {
			LOG_INF("Link closed (%d)", ret);
			break;
		}

		if (session.state != AA_HU_WAIT_VERSION) {
			continue;
		}

		if (version_tries >= VERSION_RETRY_LIMIT) {
			aa_hu_session_abort("phone never answered the version request");
			break;
		}

		version_tries++;
		LOG_DBG("Version request unanswered, retry %u", version_tries);
		if (aa_control_send_version_request() != 0) {
			aa_hu_session_abort("version request failed");
			break;
		}
	}
}

static void rx_thread(void *p1, void *p2, void *p3)
{
	const struct aa_transport *t = aa_transport_get();

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	if (t->open() != 0) {
		LOG_ERR("Transport %s failed to open", t->name);
		return;
	}

	LOG_INF("Android Auto head unit ready (transport %s)", t->name);

	for (;;) {
		if (t->wait_link(K_FOREVER) != 0) {
			k_sleep(K_MSEC(1000));
			continue;
		}

		link_up();
		rx_loop();
		aa_video_link_down();
		aa_input_link_down();
		aa_frame_reset();
		aa_hu_session_set_state(AA_HU_LINK_DOWN);
		t->close();
		k_sleep(K_MSEC(1000));
	}
}

int aa_hu_session_start(void)
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

	ret = aa_input_init();
	if (ret != 0) {
		return ret;
	}

	k_thread_create(&rx_thread_data, rx_stack, K_THREAD_STACK_SIZEOF(rx_stack), rx_thread,
			NULL, NULL, NULL, CONFIG_SAMPLE_AA_HU_RX_THREAD_PRIORITY, 0, K_NO_WAIT);
	k_thread_name_set(&rx_thread_data, "aa_hu_rx");

	return 0;
}
