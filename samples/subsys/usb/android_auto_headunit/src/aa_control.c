/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_control.h"

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"
#include "aa_tls.h"
#include "aa_video.h"

LOG_MODULE_REGISTER(aa_control, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Control channel, head unit side: the head unit requests the protocol version,
 * initiates the TLS handshake, declares authentication complete and answers the
 * phone's service discovery with the channels it offers.
 */

#define AA_VIDEO_CHANNEL_ID 1U
#define AA_INPUT_CHANNEL_ID 2U
#define AA_SENSOR_CHANNEL_ID 3U
#define AA_GUIDANCE_AUDIO_CHANNEL_ID 4U
#define AA_SYSTEM_AUDIO_CHANNEL_ID   5U
#define AA_MEDIA_AUDIO_CHANNEL_ID    6U
#define AA_MIC_CHANNEL_ID            7U

#if defined(CONFIG_SAMPLE_AA_HU_VIDEO_1280X720)
#define AA_OFFERED_RESOLUTION AA_VIDEO_RESOLUTION_1280x720
#else
#define AA_OFFERED_RESOLUTION AA_VIDEO_RESOLUTION_800x480
#endif

int aa_control_send_version_request(void)
{
	uint8_t body[4];

	sys_put_be16(AA_PROTO_MAJOR, &body[0]);
	sys_put_be16(CONFIG_SAMPLE_AA_HU_PROTO_MINOR, &body[2]);

	return aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_VERSION_REQUEST, body, sizeof(body));
}

static int send_service_discovery_response(void)
{
	ServiceDiscoveryResponse rsp = ServiceDiscoveryResponse_init_zero;
	struct aa_hu_session *s = aa_hu_session_get();
	ChannelDescriptor *video = &rsp.channels[0];
	ChannelDescriptor *input = &rsp.channels[1];
	ChannelDescriptor *sensor = &rsp.channels[2];
	ChannelDescriptor *mic = &rsp.channels[3];
	VideoConfig *cfg = &video->av_channel.video_configs[0];
	static const struct {
		uint8_t channel_id;
		int32_t audio_type;
		uint32_t sample_rate;
		uint32_t channel_count;
	} audio[] = {
		{AA_GUIDANCE_AUDIO_CHANNEL_ID, AA_AUDIO_TYPE_GUIDANCE, 16000U, 1U},
		{AA_SYSTEM_AUDIO_CHANNEL_ID, AA_AUDIO_TYPE_SYSTEM, 16000U, 1U},
		{AA_MEDIA_AUDIO_CHANNEL_ID, AA_AUDIO_TYPE_MEDIA, 48000U, 2U},
	};
	uint8_t buf[512];
	int len;

	rsp.channels_count = 4U + ARRAY_SIZE(audio);

	video->has_channel_id = true;
	video->channel_id = AA_VIDEO_CHANNEL_ID;
	video->has_av_channel = true;
	video->av_channel.has_stream_type = true;
	video->av_channel.stream_type = AA_STREAM_TYPE_VIDEO;
	video->av_channel.video_configs_count = 1;
	cfg->has_video_resolution = true;
	cfg->video_resolution = AA_OFFERED_RESOLUTION;
	cfg->has_video_fps = true;
	cfg->video_fps = AA_VIDEO_FPS_30;
	cfg->has_dpi = true;
	cfg->dpi = 160;

	input->has_channel_id = true;
	input->channel_id = AA_INPUT_CHANNEL_ID;
	input->has_input_channel = true;
	input->input_channel.has_touch_screen_config = true;
	input->input_channel.touch_screen_config.has_width = true;
	input->input_channel.touch_screen_config.width = CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH;
	input->input_channel.touch_screen_config.has_height = true;
	input->input_channel.touch_screen_config.height = CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT;

	/*
	 * A phone refuses to project without a driving status source, so a
	 * sensor channel is advertised even though the values are static.
	 */
	sensor->has_channel_id = true;
	sensor->channel_id = AA_SENSOR_CHANNEL_ID;
	sensor->has_sensor_channel = true;
	sensor->sensor_channel.sensors_count = 4;
	sensor->sensor_channel.sensors[0].has_type = true;
	sensor->sensor_channel.sensors[0].type = AA_SENSOR_TYPE_DRIVING_STATUS;
	sensor->sensor_channel.sensors[1].has_type = true;
	sensor->sensor_channel.sensors[1].type = AA_SENSOR_TYPE_NIGHT_DATA;
	sensor->sensor_channel.sensors[2].has_type = true;
	sensor->sensor_channel.sensors[2].type = AA_SENSOR_TYPE_PARKING_BRAKE;
	sensor->sensor_channel.sensors[3].has_type = true;
	sensor->sensor_channel.sensors[3].type = AA_SENSOR_TYPE_GEAR;

	strncpy(rsp.head_unit_name, CONFIG_SAMPLE_AA_HU_NAME, sizeof(rsp.head_unit_name) - 1U);
	rsp.has_head_unit_name = true;
	strncpy(rsp.car_model, "Zephyr", sizeof(rsp.car_model) - 1U);
	rsp.has_car_model = true;
	strncpy(rsp.car_year, "2026", sizeof(rsp.car_year) - 1U);
	rsp.has_car_year = true;
	strncpy(rsp.car_serial, "0000000000000001", sizeof(rsp.car_serial));
	rsp.car_serial[sizeof(rsp.car_serial) - 1U] = '\0';
	rsp.has_car_serial = true;
	rsp.has_driver_position = true;
	rsp.driver_position = AA_DRIVER_POSITION_LEFT;
	strncpy(rsp.headunit_manufacturer, "Zephyr Project",
		sizeof(rsp.headunit_manufacturer) - 1U);
	rsp.has_headunit_manufacturer = true;
	strncpy(rsp.headunit_model, "Android Auto sample", sizeof(rsp.headunit_model) - 1U);
	rsp.has_headunit_model = true;
	strncpy(rsp.sw_build, "1", sizeof(rsp.sw_build) - 1U);
	rsp.has_sw_build = true;
	strncpy(rsp.sw_version, "1.0", sizeof(rsp.sw_version) - 1U);
	rsp.has_sw_version = true;
	rsp.has_can_play_native_media_during_vr = true;
	rsp.can_play_native_media_during_vr = false;

	/*
	 * A phone expects somewhere to play media, navigation prompts and
	 * system sounds, and treats a head unit without them as incompatible.
	 */
	for (size_t i = 0; i < ARRAY_SIZE(audio); i++) {
		ChannelDescriptor *ch = &rsp.channels[4U + i];
		AudioConfig *acfg = &ch->av_channel.audio_configs[0];

		ch->has_channel_id = true;
		ch->channel_id = audio[i].channel_id;
		ch->has_av_channel = true;
		ch->av_channel.has_stream_type = true;
		ch->av_channel.stream_type = AA_STREAM_TYPE_AUDIO;
		ch->av_channel.has_audio_type = true;
		ch->av_channel.audio_type = audio[i].audio_type;
		ch->av_channel.audio_configs_count = 1;
		acfg->has_sample_rate = true;
		acfg->sample_rate = audio[i].sample_rate;
		acfg->has_bit_depth = true;
		acfg->bit_depth = 16U;
		acfg->has_channel_count = true;
		acfg->channel_count = audio[i].channel_count;

		s->audio_ch[i] = audio[i].channel_id;
	}

	/* A microphone source, which a phone requires for voice input */
	mic->has_channel_id = true;
	mic->channel_id = AA_MIC_CHANNEL_ID;
	mic->has_av_input_channel = true;
	mic->av_input_channel.has_stream_type = true;
	mic->av_input_channel.stream_type = AA_STREAM_TYPE_AUDIO;
	mic->av_input_channel.has_audio_config = true;
	mic->av_input_channel.audio_config.has_sample_rate = true;
	mic->av_input_channel.audio_config.sample_rate = 16000U;
	mic->av_input_channel.audio_config.has_bit_depth = true;
	mic->av_input_channel.audio_config.bit_depth = 16U;
	mic->av_input_channel.audio_config.has_channel_count = true;
	mic->av_input_channel.audio_config.channel_count = 1U;

	s->video_ch = AA_VIDEO_CHANNEL_ID;
	s->input_ch = AA_INPUT_CHANNEL_ID;
	s->mic_ch = AA_MIC_CHANNEL_ID;
	s->sensor_ch = AA_SENSOR_CHANNEL_ID;

	len = aa_pb_encode(buf, sizeof(buf), ServiceDiscoveryResponse_fields, &rsp);
	if (len < 0) {
		return len;
	}

	LOG_INF("Offering video, input, sensor, audio and microphone channels");

	return aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_SERVICE_DISCOVERY_RESPONSE, buf,
			   (size_t)len);
}

static void continue_handshake(const uint8_t *body, size_t len)
{
	int ret = aa_tls_handshake_input(body, len);

	if (ret == -EAGAIN) {
		return;
	}
	if (ret != 0) {
		aa_hu_session_abort("TLS handshake failed");
		return;
	}

	/* Handshake done: tell the phone authentication is complete */
	AuthCompleteIndication ind = AuthCompleteIndication_init_zero;
	uint8_t buf[8];
	int n;

	ind.has_status = true;
	ind.status = AA_STATUS_OK;
	n = aa_pb_encode(buf, sizeof(buf), AuthCompleteIndication_fields, &ind);
	if (n < 0 || aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_AUTH_COMPLETE, buf,
				 (size_t)n) != 0) {
		aa_hu_session_abort("auth complete failed");
		return;
	}

	aa_frame_set_encrypted(true);
	aa_hu_session_set_state(AA_HU_WAIT_SERVICE_DISCOVERY);
}

void aa_control_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	struct aa_hu_session *s = aa_hu_session_get();

	switch (msg_id) {
	case AA_CTRL_VERSION_RESPONSE:
		if (len >= 4U) {
			s->md_major = sys_get_be16(&body[0]);
			s->md_minor = sys_get_be16(&body[2]);
		}
		if (len >= 6U && sys_get_be16(&body[4]) != 0U) {
			aa_hu_session_abort("phone reported a version mismatch");
			return;
		}
		LOG_INF("Phone protocol version %u.%u", s->md_major, s->md_minor);
		if (aa_tls_handshake_start() == -EIO) {
			aa_hu_session_abort("cannot start the TLS handshake");
			return;
		}
		aa_hu_session_set_state(AA_HU_HANDSHAKE);
		break;

	case AA_CTRL_SSL_HANDSHAKE:
		continue_handshake(body, len);
		break;

	case AA_CTRL_SERVICE_DISCOVERY_REQUEST: {
		ServiceDiscoveryRequest req = ServiceDiscoveryRequest_init_zero;

		if (aa_pb_decode(body, len, ServiceDiscoveryRequest_fields, &req) == 0) {
			LOG_INF("Phone \"%s\" (%s)", req.has_device_name ? req.device_name : "?",
				req.has_label_text ? req.label_text : "?");
		}
		if (send_service_discovery_response() != 0) {
			aa_hu_session_abort("service discovery response failed");
			return;
		}
		aa_hu_session_set_state(AA_HU_RUNNING);
		break;
	}

	case AA_CTRL_NAVIGATION_FOCUS_REQUEST: {
		NavigationFocusRequest req = NavigationFocusRequest_init_zero;
		NavigationFocusResponse rsp = NavigationFocusResponse_init_zero;
		uint8_t buf[16];
		int n;

		(void)aa_pb_decode(body, len, NavigationFocusRequest_fields, &req);

		/* Mirror the requested type back to grant navigation focus */
		rsp.has_type = true;
		rsp.type = req.has_type ? req.type : 2U;

		n = aa_pb_encode(buf, sizeof(buf), NavigationFocusResponse_fields, &rsp);
		if (n >= 0) {
			(void)aa_msg_send(AA_CHANNEL_CONTROL, false,
					  AA_CTRL_NAVIGATION_FOCUS_RESPONSE, buf, (size_t)n);
		}
		break;
	}

	case AA_CTRL_AUDIO_FOCUS_REQUEST: {
		AudioFocusRequest req = AudioFocusRequest_init_zero;
		AudioFocusResponse rsp = AudioFocusResponse_init_zero;
		uint8_t buf[16];
		int n;

		if (aa_pb_decode(body, len, AudioFocusRequest_fields, &req) != 0) {
			break;
		}

		/*
		 * Nothing here plays audio, so focus is granted immediately and
		 * only a release is reported back as a loss.
		 */
		rsp.has_focus_state = true;
		rsp.focus_state = (req.audio_focus_type == AA_AUDIO_FOCUS_RELEASE)
					  ? AA_AUDIO_FOCUS_STATE_LOSS
					  : AA_AUDIO_FOCUS_STATE_GAIN;

		LOG_INF("Audio focus request %d, reporting state %d", req.audio_focus_type,
			rsp.focus_state);

		n = aa_pb_encode(buf, sizeof(buf), AudioFocusResponse_fields, &rsp);
		if (n >= 0) {
			(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_AUDIO_FOCUS_RESPONSE,
					  buf, (size_t)n);
		}
		break;
	}

	case AA_CTRL_PING_REQUEST:
		(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_PING_RESPONSE, body, len);
		break;

	case AA_CTRL_PING_RESPONSE:
	case AA_CTRL_NAVIGATION_FOCUS_RESPONSE:
		break;

	case AA_CTRL_SHUTDOWN_REQUEST:
		LOG_INF("Phone requested shutdown");
		(void)aa_msg_send(AA_CHANNEL_CONTROL, false, AA_CTRL_SHUTDOWN_RESPONSE, NULL, 0U);
		aa_hu_session_abort("shutdown");
		break;

	default:
		LOG_WRN("Unhandled control message 0x%04x", msg_id);
		break;
	}
}

void aa_control_channel_open(uint8_t channel, const uint8_t *body, size_t len)
{
	ChannelOpenRequest req = ChannelOpenRequest_init_zero;
	ChannelOpenResponse rsp = ChannelOpenResponse_init_zero;
	uint8_t buf[8];
	int n;

	(void)aa_pb_decode(body, len, ChannelOpenRequest_fields, &req);
	LOG_INF("Channel %u open request (priority %d)", channel, req.priority);

	rsp.has_status = true;
	rsp.status = AA_STATUS_OK;
	n = aa_pb_encode(buf, sizeof(buf), ChannelOpenResponse_fields, &rsp);
	if (n >= 0) {
		(void)aa_msg_send(channel, true, AA_CTRL_CHANNEL_OPEN_RESPONSE, buf, (size_t)n);
	}
}
