/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <lvgl.h>
#include <lvgl_zephyr.h>

#include "aa_display.h"
#include "aa_encoder.h"
#include "aa_session.h"
#include "aa_touch.h"
#include "h264_dump.h"
#include "ui.h"

LOG_MODULE_REGISTER(aa_main, CONFIG_SAMPLE_AA_LOG_LEVEL);

#ifdef CONFIG_SAMPLE_AA_H264_SELFTEST

/*
 * Boot-time check of the video pipeline: renders the screen, encodes an IDR
 * picture, injects a touch on the counter button, encodes the resulting P
 * picture and validates the Annex B structure of both.
 */

struct selftest_sink {
	uint32_t nal_count;
	uint32_t idr_slices;
	uint32_t p_slices;
	uint32_t sps;
	uint32_t pps;
	uint32_t epb_violations;
	size_t bytes;
	uint8_t last[3];
	uint32_t zeros;
	bool expect_header;
};

static struct selftest_sink st;

static void selftest_byte(struct selftest_sink *s, uint8_t b)
{
	/* Start codes are the only place where 00 00 0x (x <= 3) may occur */
	if (s->zeros >= 2U && b <= 0x03U) {
		if (b == 0x01U) {
			s->nal_count++;
			s->expect_header = true;
			s->zeros = 0U;
			return;
		}
		if (b != 0x00U) {
			s->epb_violations++;
		}
	}

	if (s->expect_header) {
		uint8_t type = b & 0x1FU;

		s->expect_header = false;
		if (type == 5U) {
			s->idr_slices++;
		} else if (type == 1U) {
			s->p_slices++;
		} else if (type == 7U) {
			s->sps++;
		} else if (type == 8U) {
			s->pps++;
		}
	}

	if (b == 0x00U) {
		s->zeros++;
	} else {
		s->zeros = 0U;
	}
}

static int selftest_sink_cb(void *ctx, const uint8_t *data, size_t len)
{
	struct selftest_sink *s = ctx;

	for (size_t i = 0; i < len; i++) {
		selftest_byte(s, data[i]);
	}
	s->bytes += len;

	return aa_dump_h264(data, len);
}

static int selftest_picture(bool idr, struct aa_frame_plan *plan)
{
	int ret;

	lvgl_lock();
	lv_refr_now(NULL);
	lvgl_unlock();

	ret = aa_encoder_prepare(idr, plan);
	if (ret != 0) {
		LOG_ERR("Self-test: nothing to encode (%d)", ret);
		return ret;
	}

	memset(&st, 0, sizeof(st));
	ret = aa_encoder_emit(plan, selftest_sink_cb, &st);
	if (ret != 0) {
		LOG_ERR("Self-test: encoding failed (%d)", ret);
		return ret;
	}

	if (st.bytes != plan->au_len || st.epb_violations != 0U) {
		LOG_ERR("Self-test: %zu bytes for %zu measured, %u forbidden sequences",
			st.bytes, plan->au_len, st.epb_violations);
		return -EIO;
	}

	return aa_dump_yuv_frame();
}

static void selftest(void)
{
	const struct aa_display_geometry *g = aa_display_geometry();
	static struct aa_frame_plan plan;
	size_t idr_bytes;
	uint32_t idr_nals;
	uint16_t tap_x;
	uint16_t tap_y;
	int ret;

	ret = selftest_picture(true, &plan);
	if (ret != 0) {
		return;
	}
	idr_bytes = st.bytes;
	idr_nals = st.nal_count;
	if (st.sps != 1U || st.pps != 1U || st.idr_slices == 0U || plan.coded_mbs != AA_MBS) {
		LOG_ERR("Self-test: bad IDR picture (sps %u pps %u slices %u)", st.sps, st.pps,
			st.idr_slices);
		return;
	}

	/* Press the counter button */
	lvgl_lock();
	ret = aa_ui_get_tap_target(&tap_x, &tap_y);
	lvgl_unlock();
	if (ret != 0) {
		LOG_ERR("Self-test: no button to press");
		return;
	}
	tap_x *= g->scale;
	tap_y *= g->scale;
	(void)aa_touch_report(tap_x, tap_y, true);
	k_sleep(K_MSEC(150));
	(void)aa_touch_report(tap_x, tap_y, false);
	k_sleep(K_MSEC(300));

	ret = selftest_picture(false, &plan);
	if (ret != 0) {
		return;
	}
	if (plan.idr || st.p_slices == 0U || plan.coded_mbs == 0U) {
		LOG_ERR("Self-test: bad P picture (%u macroblocks)", plan.coded_mbs);
		return;
	}

	aa_ui_set_night(true);
	k_sleep(K_MSEC(700));
	(void)selftest_picture(false, &plan);
	aa_ui_set_night(false);

	LOG_INF("aa: video self-test OK idr=%zu nals=%u p=%zu mbs=%u", idr_bytes, idr_nals,
		st.bytes, plan.coded_mbs);
}
#endif /* CONFIG_SAMPLE_AA_H264_SELFTEST */

int main(void)
{
	const struct device *display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	int ret;

	if (!device_is_ready(display)) {
		LOG_ERR("Display not ready");
		return 0;
	}

	(void)display_blanking_off(display);

	lvgl_lock();
	ret = aa_ui_init();
	lvgl_unlock();
	if (ret != 0) {
		LOG_ERR("UI init failed (%d)", ret);
		return 0;
	}

	ret = aa_encoder_init();
	if (ret != 0) {
		LOG_ERR("Encoder init failed (%d)", ret);
		return 0;
	}

	(void)aa_dump_open();

#ifdef CONFIG_SAMPLE_AA_H264_SELFTEST
	selftest();
#endif

	ret = aa_session_start();
	if (ret != 0) {
		LOG_ERR("Session start failed (%d)", ret);
	}

	return 0;
}
