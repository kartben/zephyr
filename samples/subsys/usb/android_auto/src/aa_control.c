/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_control.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "src/aa.pb.h"
#include "aa_display.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"
#include "aa_tls.h"
#include "aa_transport.h"
#include "ui.h"

LOG_MODULE_REGISTER(aa_control, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Control channel: version exchange, TLS handshake transport, authentication,
 * service discovery, keep-alive and shutdown.
 */

#if defined(CONFIG_SAMPLE_AA_VIDEO_1280X720)
#define AA_WANTED_RESOLUTION AA_VIDEO_RESOLUTION_1280x720
#else
#define AA_WANTED_RESOLUTION AA_VIDEO_RESOLUTION_800x480
#endif

#if defined(CONFIG_SAMPLE_AA_VIDEO_FPS_60)
#define AA_WANTED_FPS AA_VIDEO_FPS_60
#else
#define AA_WANTED_FPS AA_VIDEO_FPS_30
#endif

/* Service discovery responses are the largest messages the head unit sends */
static ServiceDiscoveryResponse sd_response;

static int send_version_response(void)
{
	uint8_t body[6];

	sys_put_be16(AA_PROTO_MAJOR, &body[0]);
	sys_put_be16(CONFIG_SAMPLE_AA_PROTO_MINOR, &body[2]);
	sys_put_be16(0U, &body[4]); /* status: match */

	return aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_VERSION_RESPONSE, body, sizeof(body));
}

static int send_service_discovery_request(void)
{
	ServiceDiscoveryRequest req = ServiceDiscoveryRequest_init_zero;
	uint8_t buf[80];
	int len;

	req.has_device_name = true;
	strncpy(req.device_name, CONFIG_SAMPLE_AA_DEVICE_NAME, sizeof(req.device_name) - 1U);
	req.has_label_text = true;
	strncpy(req.label_text, CONFIG_SAMPLE_AA_DEVICE_BRAND, sizeof(req.label_text) - 1U);

	len = aa_pb_encode(buf, sizeof(buf), ServiceDiscoveryRequest_fields, &req);
	if (len < 0) {
		return len;
	}

	return aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_SERVICE_DISCOVERY_REQUEST, buf,
			   (size_t)len);
}

static void select_video_config(struct aa_session *s, const AVChannel *av)
{
	s->video_config_idx = 0U;

	for (pb_size_t i = 0; i < av->video_configs_count; i++) {
		const VideoConfig *cfg = &av->video_configs[i];

		LOG_INF("  video config %u: resolution %d fps %d margins %ux%u dpi %u", i,
			cfg->video_resolution, cfg->video_fps, cfg->margin_width,
			cfg->margin_height, cfg->dpi);
	}

	for (pb_size_t i = 0; i < av->video_configs_count; i++) {
		const VideoConfig *cfg = &av->video_configs[i];

		if (cfg->video_resolution == AA_WANTED_RESOLUTION &&
		    (!cfg->has_video_fps || cfg->video_fps == AA_WANTED_FPS)) {
			s->video_config_idx = i;
			return;
		}
	}

	for (pb_size_t i = 0; i < av->video_configs_count; i++) {
		if (av->video_configs[i].video_resolution == AA_WANTED_RESOLUTION) {
			s->video_config_idx = i;
			return;
		}
	}

	LOG_WRN("Head unit does not offer %ux%u, using video config 0", AA_VIDEO_WIDTH,
		AA_VIDEO_HEIGHT);
}

static void on_service_discovery_response(const uint8_t *body, size_t len)
{
	struct aa_session *s = aa_session_get();

	memset(&sd_response, 0, sizeof(sd_response));
	if (aa_pb_decode(body, len, ServiceDiscoveryResponse_fields, &sd_response) != 0) {
		aa_session_abort("bad service discovery response");
		return;
	}

	LOG_INF("Head unit \"%s\", car model \"%s\", %u channels",
		sd_response.has_head_unit_name ? sd_response.head_unit_name : "?",
		sd_response.has_car_model ? sd_response.car_model : "?",
		sd_response.channels_count);

	for (pb_size_t i = 0; i < sd_response.channels_count; i++) {
		const ChannelDescriptor *ch = &sd_response.channels[i];
		uint8_t id = (uint8_t)ch->channel_id;

		if (ch->has_av_channel && ch->av_channel.stream_type == AA_STREAM_TYPE_VIDEO) {
			LOG_INF("Channel %u: video", id);
			if (!s->has_video) {
				s->has_video = true;
				s->video_ch = id;
				select_video_config(s, &ch->av_channel);
			}
		} else if (ch->has_input_channel) {
			const InputChannel *in = &ch->input_channel;

			LOG_INF("Channel %u: input, touch screen %ux%u", id,
				in->has_touch_screen_config ? in->touch_screen_config.width : 0U,
				in->has_touch_screen_config ? in->touch_screen_config.height : 0U);
			if (!s->has_input) {
				s->has_input = true;
				s->input_ch = id;
				if (in->has_touch_screen_config) {
					s->touch_width = (uint16_t)in->touch_screen_config.width;
					s->touch_height = (uint16_t)in->touch_screen_config.height;
				}
			}
		} else if (ch->has_sensor_channel) {
			LOG_INF("Channel %u: sensors (%u)", id, ch->sensor_channel.sensors_count);
			if (IS_ENABLED(CONFIG_SAMPLE_AA_SENSOR_CHANNEL) && !s->has_sensor) {
				s->has_sensor = true;
				s->sensor_ch = id;
			}
		} else if (ch->has_av_channel) {
			LOG_INF("Channel %u: audio (type %d)", id, ch->av_channel.audio_type);
		} else {
			LOG_INF("Channel %u: other", id);
		}
	}

	if (!s->has_video) {
		aa_session_abort("head unit offers no video channel");
		return;
	}

	if (s->touch_width == 0U || s->touch_height == 0U) {
		s->touch_width = AA_VIDEO_WIDTH;
		s->touch_height = AA_VIDEO_HEIGHT;
	}

	if (aa_session_open_channels() != 0) {
		aa_session_abort("channel open request failed");
	}
}

static void on_ping_request(const uint8_t *body, size_t len)
{
	PingRequest req = PingRequest_init_zero;
	PingResponse rsp = PingResponse_init_zero;
	uint8_t buf[16];
	int n;

	if (aa_pb_decode(body, len, PingRequest_fields, &req) != 0) {
		return;
	}

	rsp.has_timestamp = true;
	rsp.timestamp = req.timestamp;
	n = aa_pb_encode(buf, sizeof(buf), PingResponse_fields, &rsp);
	if (n >= 0) {
		(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_PING_RESPONSE, buf, (size_t)n);
	}
}

static void on_navigation_focus_request(const uint8_t *body, size_t len)
{
	NavigationFocusRequest req = NavigationFocusRequest_init_zero;
	NavigationFocusResponse rsp = NavigationFocusResponse_init_zero;
	uint8_t buf[16];
	int n;

	if (aa_pb_decode(body, len, NavigationFocusRequest_fields, &req) != 0) {
		return;
	}

	rsp.has_type = true;
	rsp.type = req.type;
	n = aa_pb_encode(buf, sizeof(buf), NavigationFocusResponse_fields, &rsp);
	if (n >= 0) {
		(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_NAVIGATION_FOCUS_RESPONSE, buf,
				  (size_t)n);
	}
}

void aa_control_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	struct aa_session *s = aa_session_get();
	int ret;

	switch (msg_id) {
	case AA_CTRL_VERSION_REQUEST:
		if (s->state != AA_STATE_WAIT_VERSION) {
			/* A head unit that went away silently (USB stays configured) came back */
			LOG_WRN("Version request during a session, starting over");
			aa_session_restart();
		}
		if (len >= 4U) {
			s->hu_major = sys_get_be16(&body[0]);
			s->hu_minor = sys_get_be16(&body[2]);
		}
		LOG_INF("Head unit protocol version %u.%u", s->hu_major, s->hu_minor);
		if (send_version_response() != 0) {
			aa_session_abort("version response failed");
			return;
		}
		aa_session_set_state(AA_STATE_HANDSHAKE);
		break;

	case AA_CTRL_SSL_HANDSHAKE:
		ret = aa_tls_handshake_input(body, len);
		if (ret == 0) {
			aa_session_set_state(AA_STATE_WAIT_AUTH);
		} else if (ret != -EAGAIN) {
			aa_session_abort("TLS handshake failed");
		}
		break;

	case AA_CTRL_AUTH_COMPLETE: {
		AuthCompleteIndication ind = AuthCompleteIndication_init_zero;

		if (aa_pb_decode(body, len, AuthCompleteIndication_fields, &ind) != 0 ||
		    (ind.has_status && ind.status != AA_STATUS_OK)) {
			aa_session_abort("authentication refused");
			return;
		}
		if (!aa_tls_handshake_done()) {
			aa_session_abort("auth complete before the handshake");
			return;
		}
		LOG_INF("Authenticated, encryption on");
		aa_frame_set_encrypted(true);
		aa_ui_set_link_state(AA_UI_LINK_SECURE);
		if (send_service_discovery_request() != 0) {
			aa_session_abort("service discovery request failed");
			return;
		}
		aa_session_set_state(AA_STATE_WAIT_SERVICE_DISCOVERY);
		break;
	}

	case AA_CTRL_SERVICE_DISCOVERY_RESPONSE:
		on_service_discovery_response(body, len);
		break;

	case AA_CTRL_PING_REQUEST:
		on_ping_request(body, len);
		break;

	case AA_CTRL_PING_RESPONSE:
		break;

	case AA_CTRL_NAVIGATION_FOCUS_REQUEST:
		on_navigation_focus_request(body, len);
		break;

	case AA_CTRL_SHUTDOWN_REQUEST:
		LOG_INF("Head unit requested shutdown");
		(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_SHUTDOWN_RESPONSE, NULL, 0U);
		aa_session_set_state(AA_STATE_SHUTDOWN);
		aa_session_abort("shutdown");
		break;

	case AA_CTRL_VOICE_SESSION_REQUEST:
	case AA_CTRL_AUDIO_FOCUS_REQUEST:
	case AA_CTRL_AUDIO_FOCUS_RESPONSE:
	case AA_CTRL_NAVIGATION_FOCUS_RESPONSE:
		LOG_INF("Ignoring control message 0x%04x", msg_id);
		break;

	default:
		LOG_WRN("Unknown control message 0x%04x (%zu bytes)", msg_id, len);
		break;
	}
}
