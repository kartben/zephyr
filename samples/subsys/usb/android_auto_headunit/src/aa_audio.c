/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_audio.h"

#include <zephyr/logging/log.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_play.h"
#include "aa_session.h"

LOG_MODULE_REGISTER(aa_audio, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Audio channels. A phone expects a head unit to provide media, speech and
 * system audio sinks, and refuses to project without them. The sample has
 * nowhere to play the samples, so it sets the streams up and acknowledges the
 * media messages without decoding them.
 */

static int32_t sessions[ARRAY_SIZE(((struct aa_hu_session *)0)->audio_ch)];

/* The order service discovery offers the sinks in */
static const int32_t audio_types[] = {
	AA_AUDIO_TYPE_GUIDANCE,
	AA_AUDIO_TYPE_SYSTEM,
	AA_AUDIO_TYPE_MEDIA,
};
BUILD_ASSERT(ARRAY_SIZE(audio_types) == ARRAY_SIZE(sessions),
	     "one audio type per advertised channel");

static int channel_index(uint8_t channel)
{
	const struct aa_hu_session *s = aa_hu_session_get();

	for (size_t i = 0; i < ARRAY_SIZE(s->audio_ch); i++) {
		if (s->audio_ch[i] == channel) {
			return (int)i;
		}
	}

	return -1;
}

bool aa_audio_is_audio_channel(uint8_t channel)
{
	return channel_index(channel) >= 0;
}

static void send_setup_response(uint8_t channel)
{
	AVChannelSetupResponse rsp = AVChannelSetupResponse_init_zero;
	uint8_t buf[32];
	int n;

	rsp.has_media_status = true;
	rsp.media_status = AA_MEDIA_STATUS_OK;
	rsp.has_max_unacked = true;
	/*
	 * A phone holds a media message until the one before it is answered,
	 * so allowing only one in flight caps the stream at whatever the round
	 * trip allows and the phone drops the rest. Audio cannot be late, so
	 * let it keep the link full.
	 */
	rsp.max_unacked = CONFIG_SAMPLE_AA_HU_AUDIO_MAX_UNACKED;
	rsp.configs_count = 1;
	rsp.configs[0] = 0;

	n = aa_pb_encode(buf, sizeof(buf), AVChannelSetupResponse_fields, &rsp);
	if (n >= 0) {
		(void)aa_msg_send(channel, false, AA_AV_SETUP_RESPONSE, buf, (size_t)n);
	}
}

static void send_media_ack(uint8_t channel, int idx)
{
	AVMediaAckIndication ack = AVMediaAckIndication_init_zero;
	uint8_t buf[16];
	int n;

	ack.has_session = true;
	ack.session = sessions[idx];
	ack.has_value = true;
	ack.value = 1;

	n = aa_pb_encode(buf, sizeof(buf), AVMediaAckIndication_fields, &ack);
	if (n >= 0) {
		(void)aa_msg_send(channel, false, AA_AV_MEDIA_ACK_INDICATION, buf, (size_t)n);
	}
}

void aa_audio_handle(uint8_t channel, uint16_t msg_id, const uint8_t *body, size_t len)
{
	int idx = channel_index(channel);

	if (idx < 0) {
		return;
	}

	switch (msg_id) {
	case AA_AV_SETUP_REQUEST:
		send_setup_response(channel);
		break;

	case AA_AV_START_INDICATION: {
		AVChannelStartIndication ind = AVChannelStartIndication_init_zero;

		if (aa_pb_decode(body, len, AVChannelStartIndication_fields, &ind) == 0) {
			sessions[idx] = ind.session;
			LOG_INF("Audio channel %u started, session %d", channel, ind.session);
		}
		break;
	}

	case AA_AV_STOP_INDICATION:
		aa_play_flush(audio_types[idx]);
		break;

	case AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION:
	case AA_AV_MEDIA_INDICATION: {
		/* A timestamped message puts the time before the samples */
		size_t skip = (msg_id == AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION) ? sizeof(uint64_t)
									       : 0U;

		/*
		 * Acknowledged first: the phone holds off until it is, and the
		 * samples are of no use late.
		 */
		send_media_ack(channel, idx);
		if (len > skip) {
			aa_play_submit(audio_types[idx], &body[skip], len - skip);
		}
		break;
	}

	default:
		LOG_DBG("Unhandled audio message 0x%04x on channel %u", msg_id, channel);
		break;
	}
}
