/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_video.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_gui.h"
#include "aa_ids.h"
#include "aa_layout.h"
#include "aa_screen.h"
#include "aa_session.h"
#include "aa_h264.h"
#include "h264_ipcm_decode.h"

LOG_MODULE_REGISTER(aa_video, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Video channel, head unit side: negotiates the stream, grants video focus,
 * decodes each media message into the framebuffer and shows it on the display,
 * and acknowledges every message so the phone keeps streaming.
 */

#define VIDEO_WIDTH  CONFIG_SAMPLE_AA_HU_VIDEO_WIDTH
#define VIDEO_HEIGHT CONFIG_SAMPLE_AA_HU_VIDEO_HEIGHT
#define MEDIA_TIMESTAMP_LEN 8U

static struct h264_ipcm_dec decoder;
static uint8_t nal_scratch[CONFIG_SAMPLE_AA_HU_NAL_SCRATCH_SIZE];
static uint32_t frames;
static int64_t stats_ms;
static uint32_t stats_frames;

int aa_video_init(void)
{
	int ret = aa_screen_init();

	if (ret != 0) {
		return ret;
	}

	if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_H264)) {
		ret = aa_h264_init();
	} else {
		ret = h264_ipcm_decode_init(&decoder, VIDEO_WIDTH, VIDEO_HEIGHT,
					    aa_screen_framebuffer(), nal_scratch,
					    sizeof(nal_scratch));
	}
	if (ret != 0) {
		return ret;
	}

	return aa_gui_init();
}

void aa_video_link_up(void)
{
	aa_screen_blank(false);

	frames = 0;
	stats_frames = 0;
	stats_ms = k_uptime_get();
}

void aa_video_link_down(void)
{
	/*
	 * The phone is gone, so do not leave the last picture of the session on
	 * the panel, and drop the decoder state with it: whatever comes back
	 * starts a stream of its own.
	 */
	if (aa_layout_gui_visible()) {
		/* The GUI is still worth showing, so only the video goes dark */
		aa_screen_show(NULL, 0U, 0U);
	} else {
		aa_screen_blank(true);
	}

	if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_H264)) {
		(void)aa_h264_reset();
	} else {
		(void)h264_ipcm_decode_init(&decoder, VIDEO_WIDTH, VIDEO_HEIGHT,
					    aa_screen_framebuffer(), nal_scratch,
					    sizeof(nal_scratch));
	}
}

static int send_focus(int32_t mode)
{
	VideoFocusIndication ind = VideoFocusIndication_init_zero;
	uint8_t buf[8];
	int n;

	ind.has_focus_mode = true;
	ind.focus_mode = mode;
	ind.has_unrequested = true;
	ind.unrequested = false;

	n = aa_pb_encode(buf, sizeof(buf), VideoFocusIndication_fields, &ind);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(aa_hu_session_get()->video_ch, false, AA_AV_VIDEO_FOCUS_INDICATION, buf,
			   (size_t)n);
}

static int send_focus_indication(void)
{
	return send_focus(AA_VIDEO_FOCUS_FOCUSED);
}

/*
 * A phone sends parameter sets with key frames and nothing in between, so a
 * decoder that has just started over has nothing to decode against. Giving
 * focus up and taking it back is what asks for the next one.
 */
static void request_key_frame(void)
{
	(void)send_focus(AA_VIDEO_FOCUS_UNFOCUSED);
	(void)send_focus_indication();
}

static void on_setup_request(const uint8_t *body, size_t len)
{
	AVChannelSetupResponse rsp = AVChannelSetupResponse_init_zero;
	AVChannelSetupRequest req = AVChannelSetupRequest_init_zero;
	uint8_t buf[16];
	int n;

	(void)aa_pb_decode(body, len, AVChannelSetupRequest_fields, &req);
	LOG_INF("Video setup request, codec type %d", req.codec_type);

	rsp.has_media_status = true;
	rsp.media_status = AA_MEDIA_STATUS_OK;
	rsp.has_max_unacked = true;
	rsp.max_unacked = CONFIG_SAMPLE_AA_HU_MAX_UNACKED;
	rsp.configs_count = 1;
	rsp.configs[0] = 0;

	n = aa_pb_encode(buf, sizeof(buf), AVChannelSetupResponse_fields, &rsp);
	if (n < 0 || aa_msg_send(aa_hu_session_get()->video_ch, false, AA_AV_SETUP_RESPONSE, buf,
				 (size_t)n) != 0) {
		aa_hu_session_abort("video setup response failed");
		return;
	}

	(void)send_focus_indication();
}

/*
 * Report what the phone is sending. The bundled decoder only understands the
 * companion sample's I_PCM stream, so log the sequence parameter set once to
 * show which profile a real decoder would have to handle.
 */
static void log_stream_profile(const uint8_t *au, size_t len)
{
	static bool logged;

	if (logged) {
		return;
	}

	for (size_t i = 0; i + 5U < len; i++) {
		uint8_t nal_type;

		if (au[i] != 0U || au[i + 1U] != 0U || au[i + 2U] != 1U) {
			continue;
		}

		nal_type = au[i + 3U] & 0x1FU;
		if (nal_type != 7U) {
			continue;
		}

		LOG_INF("Phone stream: H.264 profile_idc %u constraints 0x%02x level_idc %u",
			au[i + 4U], au[i + 5U], au[i + 6U]);
		logged = true;
		return;
	}
}

/* The picture is already on the display; this only counts it */
static void show_frame(void)
{
	int64_t now;

	frames++;
	stats_frames++;
	now = k_uptime_get();
	if ((now - stats_ms) >= 2000) {
		LOG_INF("Video: %u frames, %u.%u fps", frames,
			(stats_frames * 1000U) / (uint32_t)(now - stats_ms),
			((stats_frames * 10000U) / (uint32_t)(now - stats_ms)) % 10U);
		stats_ms = now;
		stats_frames = 0;
	}
}

static void on_media(const uint8_t *body, size_t len, bool has_timestamp)
{
	struct aa_hu_session *s = aa_hu_session_get();
	AVMediaAckIndication ack = AVMediaAckIndication_init_zero;
	const uint8_t *au = body;
	size_t au_len = len;
	uint8_t buf[16];
	int n;

	if (has_timestamp) {
		if (len < MEDIA_TIMESTAMP_LEN) {
			return;
		}
		au += MEDIA_TIMESTAMP_LEN;
		au_len -= MEDIA_TIMESTAMP_LEN;
	}

	log_stream_profile(au, au_len);
	/*
	 * Acknowledge before decoding rather than after. The phone sends one
	 * picture at a time and waits for this, so acknowledging at the end of
	 * the pipeline left it idle for as long as a picture took to decode and
	 * reach the panel, and every picture arrived that much later than it
	 * could have. The picture has already been taken out of the receive
	 * buffer by now, and the next one cannot overtake it: they are read and
	 * decoded in turn on this thread.
	 */
	ack.has_session = true;
	ack.session = s->video_session;
	ack.has_value = true;
	ack.value = 1;
	n = aa_pb_encode(buf, sizeof(buf), AVMediaAckIndication_fields, &ack);
	if (n >= 0) {
		(void)aa_msg_send(s->video_ch, false, AA_AV_MEDIA_ACK_INDICATION, buf, (size_t)n);
	}


	if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_H264)) {
		/* Only a positive result means a picture reached the buffer */
		n = aa_h264_decode_au(au, au_len);
		if (n > 0) {
			show_frame();
		}
	} else {
		n = h264_ipcm_decode_au(&decoder, au, au_len);
		if (n >= 0) {
			aa_screen_push();
			show_frame();
		}
	}

	if (n == -ENOTSUP) {
		LOG_WRN_ONCE("Stream needs a full H.264 decoder (not I_PCM only)");
	} else if (n == -EAGAIN) {
		request_key_frame();
	} else if (n < 0 && n != -ENODATA) {
		LOG_WRN("Decode error %d", n);
	}

}

void aa_video_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	struct aa_hu_session *s = aa_hu_session_get();

	switch (msg_id) {
	case AA_AV_SETUP_REQUEST:
		on_setup_request(body, len);
		break;

	case AA_AV_START_INDICATION: {
		AVChannelStartIndication ind = AVChannelStartIndication_init_zero;

		if (aa_pb_decode(body, len, AVChannelStartIndication_fields, &ind) == 0) {
			s->video_session = ind.session;
			LOG_INF("Video stream started, session %d", ind.session);
		}
		break;
	}

	case AA_AV_VIDEO_FOCUS_REQUEST:
		(void)send_focus_indication();
		break;

	case AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION:
		on_media(body, len, true);
		break;

	case AA_AV_MEDIA_INDICATION:
		on_media(body, len, false);
		break;

	default:
		LOG_WRN("Unhandled video message 0x%04x", msg_id);
		break;
	}
}
