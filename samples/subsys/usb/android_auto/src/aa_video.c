/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_video.h"

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "src/aa.pb.h"
#include "aa_display.h"
#include "aa_encoder.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"
#include "h264_dump.h"
#include "ui.h"

LOG_MODULE_REGISTER(aa_video, CONFIG_SAMPLE_AA_LOG_LEVEL);

/*
 * Video channel: negotiates the stream, tracks video focus and the number of
 * media messages the head unit accepts without acknowledging, and runs the
 * thread that turns dirty macroblocks into media messages.
 */

#define FRAME_PERIOD_MS   (1000U / CONFIG_SAMPLE_AA_MAX_FPS)
#define FOCUS_REQUEST_MS  1000
#define MEDIA_TIMESTAMP_LEN 8U
/* Consecutive acknowledgment timeouts after which the head unit is considered gone */
#define MAX_MISSED_ACKS 3U

static K_THREAD_STACK_DEFINE(video_stack, CONFIG_SAMPLE_AA_VIDEO_STACK_SIZE);
static struct k_thread video_thread_data;
static struct k_sem unacked_sem;
static K_SEM_DEFINE(stream_sem, 0, 1);
static struct k_work_delayable focus_work;

static bool link_up;
static bool setup_ok;
static bool started;
static bool focused;
static uint32_t max_unacked;
static atomic_t streaming;
static atomic_t idr_request;
static atomic_t config_request;
static uint32_t pending_mbs;

static struct aa_frame_plan plan;

bool aa_video_is_streaming(void)
{
	return atomic_get(&streaming) != 0;
}

static void update_streaming(void)
{
	bool now = link_up && setup_ok && started && focused;

	if (now && !aa_video_is_streaming()) {
		LOG_INF("Streaming %ux%u to the head unit", AA_VIDEO_WIDTH, AA_VIDEO_HEIGHT);
		atomic_set(&idr_request, 1);
		if (IS_ENABLED(CONFIG_SAMPLE_AA_VIDEO_CODEC_CONFIG_MSG)) {
			atomic_set(&config_request, 1);
		}
		atomic_set(&streaming, 1);
		aa_ui_set_link_state(AA_UI_LINK_STREAMING);
		k_sem_give(&stream_sem);
	} else if (!now && aa_video_is_streaming()) {
		LOG_INF("Streaming stopped");
		atomic_set(&streaming, 0);
		aa_ui_set_link_state(link_up ? AA_UI_LINK_SECURE : AA_UI_LINK_DOWN);
	}
}

static int send_focus_request(void)
{
	VideoFocusRequest req = VideoFocusRequest_init_zero;
	uint8_t buf[16];
	int n;

	req.has_disp_index = true;
	req.disp_index = 0;
	req.has_focus_mode = true;
	req.focus_mode = AA_VIDEO_FOCUS_FOCUSED;
	req.has_focus_reason = true;
	req.focus_reason = 0;

	n = aa_pb_encode(buf, sizeof(buf), VideoFocusRequest_fields, &req);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(aa_session_get()->video_ch, false, AA_AV_VIDEO_FOCUS_REQUEST, buf,
			   (size_t)n);
}

static void focus_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (link_up && setup_ok && !focused) {
		LOG_INF("Requesting video focus");
		(void)send_focus_request();
	}
}

void aa_video_link_up(void)
{
	link_up = true;
	setup_ok = false;
	started = false;
	focused = false;
	pending_mbs = 0U;
	update_streaming();
}

void aa_video_link_down(void)
{
	link_up = false;
	setup_ok = false;
	started = false;
	focused = false;
	(void)k_work_cancel_delayable(&focus_work);
	update_streaming();
	k_sem_reset(&unacked_sem);
}

int aa_video_setup(void)
{
	AVChannelSetupRequest req = AVChannelSetupRequest_init_zero;
	struct aa_session *s = aa_session_get();
	uint8_t buf[16];
	int n;

	req.has_config_index = true;
	req.config_index = s->video_config_idx;

	n = aa_pb_encode(buf, sizeof(buf), AVChannelSetupRequest_fields, &req);
	if (n < 0) {
		return n;
	}

	LOG_INF("Video setup request, config %u", s->video_config_idx);

	return aa_msg_send(s->video_ch, false, AA_AV_SETUP_REQUEST, buf, (size_t)n);
}

static int send_start_indication(void)
{
	AVChannelStartIndication ind = AVChannelStartIndication_init_zero;
	struct aa_session *s = aa_session_get();
	uint8_t buf[16];
	int n;

	ind.has_session = true;
	ind.session = 0;
	ind.has_config = true;
	ind.config = s->video_config_idx;

	n = aa_pb_encode(buf, sizeof(buf), AVChannelStartIndication_fields, &ind);
	if (n < 0) {
		return n;
	}

	return aa_msg_send(s->video_ch, false, AA_AV_START_INDICATION, buf, (size_t)n);
}

static void on_setup_response(const uint8_t *body, size_t len)
{
	AVChannelSetupResponse rsp = AVChannelSetupResponse_init_zero;

	if (aa_pb_decode(body, len, AVChannelSetupResponse_fields, &rsp) != 0) {
		aa_session_abort("bad video setup response");
		return;
	}

	if (rsp.has_media_status && rsp.media_status != AA_MEDIA_STATUS_OK) {
		aa_session_abort("video setup refused");
		return;
	}

	max_unacked = rsp.has_max_unacked ? rsp.max_unacked : 1U;
	max_unacked = CLAMP(max_unacked, 1U, CONFIG_SAMPLE_AA_VIDEO_MAX_UNACKED_CAP);
	k_sem_init(&unacked_sem, max_unacked, max_unacked);
	LOG_INF("Video setup ok, %u unacknowledged media messages allowed", max_unacked);

	if (send_start_indication() != 0) {
		aa_session_abort("video start indication failed");
		return;
	}

	setup_ok = true;
	started = true;
	aa_session_set_state(AA_STATE_RUNNING);
	(void)k_work_schedule(&focus_work, K_MSEC(FOCUS_REQUEST_MS));
	update_streaming();
}

static void on_focus_indication(const uint8_t *body, size_t len)
{
	VideoFocusIndication ind = VideoFocusIndication_init_zero;

	if (aa_pb_decode(body, len, VideoFocusIndication_fields, &ind) != 0) {
		return;
	}

	LOG_INF("Video focus %d%s", ind.focus_mode, ind.unrequested ? " (unrequested)" : "");
	focused = (ind.focus_mode == AA_VIDEO_FOCUS_FOCUSED);
	update_streaming();
}

void aa_video_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	switch (msg_id) {
	case AA_AV_SETUP_RESPONSE:
		on_setup_response(body, len);
		break;
	case AA_AV_VIDEO_FOCUS_INDICATION:
		on_focus_indication(body, len);
		break;
	case AA_AV_MEDIA_ACK_INDICATION:
		k_sem_give(&unacked_sem);
		break;
	case AA_AV_STOP_INDICATION:
		LOG_INF("Head unit stopped the video stream");
		started = false;
		update_streaming();
		break;
	default:
		LOG_WRN("Unhandled video message 0x%04x (%zu bytes)", msg_id, len);
		break;
	}
}

static int au_sink(void *ctx, const uint8_t *data, size_t len)
{
	ARG_UNUSED(ctx);

	(void)aa_dump_h264(data, len);

	return aa_msg_write(data, len);
}

static int send_codec_config(uint8_t channel)
{
	int ret;

	ret = aa_msg_begin(channel, AA_AV_MEDIA_INDICATION, aa_encoder_config_len());
	if (ret != 0) {
		return ret;
	}

	ret = aa_encoder_write_config(au_sink, NULL);
	if (ret < 0) {
		aa_msg_abort();
		return ret;
	}

	return aa_msg_end();
}

static int send_frame(uint8_t channel)
{
	uint8_t ts[MEDIA_TIMESTAMP_LEN];
	int ret;

	sys_put_be64(k_ticks_to_us_floor64(k_uptime_ticks()), ts);

	ret = aa_msg_begin(channel, AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION,
			   MEDIA_TIMESTAMP_LEN + plan.au_len);
	if (ret != 0) {
		return ret;
	}

	ret = aa_msg_write(ts, sizeof(ts));
	if (ret == 0) {
		ret = aa_encoder_emit(&plan, au_sink, NULL);
	}

	if (ret != 0) {
		aa_msg_abort();
		return ret;
	}

	ret = aa_msg_end();
	if (ret == 0) {
		(void)aa_dump_yuv_frame();
	}

	return ret;
}

/* Hold the thread so that a picture does not exceed the configured output rate */
static void rate_limit(size_t bytes, int64_t spent_ms)
{
#if CONFIG_SAMPLE_AA_MAX_KBPS > 0
	int64_t budget_ms = ((int64_t)bytes * 8) / CONFIG_SAMPLE_AA_MAX_KBPS;

	if (budget_ms > spent_ms) {
		k_sleep(K_MSEC((uint32_t)(budget_ms - spent_ms)));
	}
#else
	ARG_UNUSED(bytes);
	ARG_UNUSED(spent_ms);
#endif
}

static void video_thread(void *p1, void *p2, void *p3)
{
	int64_t last_frame_ms = 0;
	uint32_t missed_acks = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		uint8_t channel = aa_session_get()->video_ch;
		int64_t now;
		int ret;

		if (!aa_video_is_streaming()) {
			k_sem_take(&stream_sem, K_FOREVER);
			continue;
		}

		if (atomic_cas(&config_request, 1, 0)) {
			ret = send_codec_config(channel);
			if (ret != 0) {
				LOG_WRN("Codec config message failed (%d)", ret);
			}
		}

		if (pending_mbs == 0U && atomic_get(&idr_request) == 0) {
			if (aa_display_wait_frame(K_MSEC(200)) != 0) {
				continue;
			}
		}

		now = k_uptime_get();
		if ((now - last_frame_ms) < FRAME_PERIOD_MS) {
			k_sleep(K_MSEC(FRAME_PERIOD_MS - (uint32_t)(now - last_frame_ms)));
		}

		ret = aa_encoder_prepare(atomic_cas(&idr_request, 1, 0), &plan);
		if (ret == -EAGAIN) {
			pending_mbs = 0U;
			continue;
		}
		if (ret != 0) {
			k_sleep(K_MSEC(100));
			continue;
		}
		pending_mbs = plan.pending_mbs;

		if (k_sem_take(&unacked_sem, K_MSEC(CONFIG_SAMPLE_AA_VIDEO_ACK_TIMEOUT_MS)) != 0) {
			LOG_WRN("No media acknowledgment within %u ms",
				CONFIG_SAMPLE_AA_VIDEO_ACK_TIMEOUT_MS);
			if (++missed_acks >= MAX_MISSED_ACKS) {
				aa_session_abort("head unit stopped acknowledging media");
				atomic_set(&idr_request, 1);
				continue;
			}
		} else {
			missed_acks = 0U;
		}

		if (!aa_video_is_streaming()) {
			atomic_set(&idr_request, 1);
			continue;
		}

		ret = send_frame(channel);
		if (ret != 0) {
			LOG_WRN("Media message failed (%d)", ret);
			atomic_set(&idr_request, 1);
			k_sleep(K_MSEC(100));
			continue;
		}

		LOG_DBG("%s frame: %u macroblocks, %zu bytes, %u pending", plan.idr ? "IDR" : "P",
			plan.coded_mbs, plan.au_len, plan.pending_mbs);

		last_frame_ms = k_uptime_get();
		rate_limit(plan.au_len, last_frame_ms - now);
	}
}

int aa_video_init(void)
{
	k_sem_init(&unacked_sem, 1, 1);
	k_work_init_delayable(&focus_work, focus_work_handler);

	k_thread_create(&video_thread_data, video_stack, K_THREAD_STACK_SIZEOF(video_stack),
			video_thread, NULL, NULL, NULL, CONFIG_SAMPLE_AA_VIDEO_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&video_thread_data, "aa_video");

	return 0;
}
