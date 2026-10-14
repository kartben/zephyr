/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_video_edge264

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/video/video.h>

#include <edge264.h>

#include "video_common.h"

LOG_MODULE_REGISTER(video_edge264, CONFIG_VIDEO_LOG_LEVEL);

struct video_common_header {
	struct video_format fmt;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	bool is_streaming;
};

struct video_m2m_common {
	struct video_common_header in;
	struct video_common_header out;
};

struct video_edge264_data {
	const struct device *dev;
	struct video_m2m_common m2m;
	struct k_mutex lock;
	struct k_work work;
	Edge264Decoder *dec;
};

static K_THREAD_STACK_DEFINE(edge264_wq_stack, CONFIG_VIDEO_EDGE264_STACK_SIZE);
static struct k_work_q edge264_wq;
static bool edge264_wq_started;

static const struct video_format_cap in_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_H264,
		.width_min = 16,
		.width_max = CONFIG_VIDEO_EDGE264_MAX_WIDTH,
		.height_min = 16,
		.height_max = CONFIG_VIDEO_EDGE264_MAX_HEIGHT,
		.width_step = 16,
		.height_step = 16,
	},
	{0},
};

static const struct video_format_cap out_fmts[] = {
	{
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width_min = 16,
		.width_max = CONFIG_VIDEO_EDGE264_MAX_WIDTH,
		.height_min = 16,
		.height_max = CONFIG_VIDEO_EDGE264_MAX_HEIGHT,
		.width_step = 16,
		.height_step = 16,
	},
	{
		.pixelformat = VIDEO_PIX_FMT_YUV420,
		.width_min = 16,
		.width_max = CONFIG_VIDEO_EDGE264_MAX_WIDTH,
		.height_min = 16,
		.height_max = CONFIG_VIDEO_EDGE264_MAX_HEIGHT,
		.width_step = 16,
		.height_step = 16,
	},
	{0},
};

static void edge264_wq_ensure_started(void)
{
	if (edge264_wq_started) {
		return;
	}

	k_work_queue_init(&edge264_wq);
	k_work_queue_start(&edge264_wq, edge264_wq_stack, K_THREAD_STACK_SIZEOF(edge264_wq_stack),
			   CONFIG_SYSTEM_WORKQUEUE_PRIORITY, NULL);
	k_thread_name_set(edge264_wq.thread_id, "edge264");
	edge264_wq_started = true;
}

static int clamp_u8(int v)
{
	if (v < 0) {
		return 0;
	}
	if (v > 255) {
		return 255;
	}
	return v;
}

static uint16_t yuv_to_rgb565(int y, int u, int v)
{
	int c = y - 16;
	int d = u - 128;
	int e = v - 128;
	int r = clamp_u8((298 * c + 409 * e + 128) >> 8);
	int g = clamp_u8((298 * c - 100 * d - 208 * e + 128) >> 8);
	int b = clamp_u8((298 * c + 516 * d + 128) >> 8);

	return (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
}

static void copy_yuv420(const Edge264Frame *frm, uint8_t *dst, uint16_t dst_pitch)
{
	const uint8_t *src;
	uint8_t *d;
	int y;

	src = frm->samples[0];
	d = dst;
	for (y = 0; y < frm->height_Y; y++) {
		memcpy(d, src, frm->width_Y);
		src += frm->stride_Y;
		d += dst_pitch;
	}

	src = frm->samples[1];
	d = dst + dst_pitch * frm->height_Y;
	for (y = 0; y < frm->height_C; y++) {
		memcpy(d, src, frm->width_C);
		src += frm->stride_C;
		d += dst_pitch / 2;
	}

	src = frm->samples[2];
	d = dst + dst_pitch * frm->height_Y + (dst_pitch / 2) * frm->height_C;
	for (y = 0; y < frm->height_C; y++) {
		memcpy(d, src, frm->width_C);
		src += frm->stride_C;
		d += dst_pitch / 2;
	}
}

static void copy_rgb565(const Edge264Frame *frm, uint8_t *dst, uint16_t dst_pitch)
{
	int y;
	int x;

	for (y = 0; y < frm->height_Y; y++) {
		const uint8_t *ys = frm->samples[0] + y * frm->stride_Y;
		const uint8_t *us = frm->samples[1] + (y / 2) * frm->stride_C;
		const uint8_t *vs = frm->samples[2] + (y / 2) * frm->stride_C;
		uint16_t *out = (uint16_t *)(dst + y * dst_pitch);

		for (x = 0; x < frm->width_Y; x++) {
			out[x] = yuv_to_rgb565(ys[x], us[x / 2], vs[x / 2]);
		}
	}
}

static int emit_frame(struct video_edge264_data *data, const Edge264Frame *frm,
		      struct video_buffer *out)
{
	const struct video_format *fmt = &data->m2m.out.fmt;
	size_t need;

	if ((frm->width_Y != (int16_t)fmt->width) || (frm->height_Y != (int16_t)fmt->height)) {
		LOG_WRN("Decoded %dx%d does not match output %ux%u", frm->width_Y, frm->height_Y,
			fmt->width, fmt->height);
	}

	need = fmt->size;
	if (out->size < need) {
		LOG_ERR("Output buffer too small (%u < %u)", out->size, (uint32_t)need);
		return -ENOMEM;
	}

	if (fmt->pixelformat == VIDEO_PIX_FMT_RGB565) {
		copy_rgb565(frm, out->buffer, fmt->pitch);
	} else {
		copy_yuv420(frm, out->buffer, fmt->pitch);
	}

	out->bytesused = (uint32_t)need;
	out->line_offset = 0;
	return 0;
}

static int decode_access_unit(struct video_edge264_data *data, const uint8_t *buf, size_t len,
			       struct video_buffer *out)
{
	const uint8_t *end = buf + len;
	const uint8_t *nal;
	const uint8_t *start_code;
	Edge264Frame frm;
	int ret;
	int got = 0;

	if ((buf == NULL) || (len < 4U)) {
		return -EINVAL;
	}

	edge264_flush(data->dec);

	nal = buf + 3 + (buf[2] == 0);

	while (nal < end) {
		start_code = edge264_find_start_code(nal, end, 0);
		ret = edge264_decode_NAL(data->dec, nal, start_code, NULL, NULL);
		if ((ret != 0) && (ret != ENOBUFS) && (ret != ENOTSUP) && (ret != ENOMSG) &&
		    (ret != ENODATA)) {
			LOG_ERR("edge264_decode_NAL failed: %d", ret);
			return -EIO;
		}

		while (edge264_get_frame(data->dec, &frm, 0) == 0) {
			if (emit_frame(data, &frm, out) == 0) {
				got++;
			}
		}

		if (got > 0) {
			break;
		}

		if (ret == ENOBUFS) {
			continue;
		}
		if (start_code >= end) {
			break;
		}
		nal = start_code + 3U;
	}

	if (got == 0) {
		return -EAGAIN;
	}

	return 0;
}

static void video_edge264_worker(struct k_work *work)
{
	struct video_edge264_data *data = CONTAINER_OF(work, struct video_edge264_data, work);
	struct video_buffer *in_buf;
	struct video_buffer *out_buf;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	while (data->m2m.in.is_streaming && data->m2m.out.is_streaming) {
		in_buf = k_fifo_get(&data->m2m.in.fifo_in, K_NO_WAIT);
		out_buf = k_fifo_get(&data->m2m.out.fifo_in, K_NO_WAIT);
		if ((in_buf == NULL) || (out_buf == NULL)) {
			if (in_buf != NULL) {
				k_fifo_put(&data->m2m.in.fifo_in, in_buf);
			}
			if (out_buf != NULL) {
				k_fifo_put(&data->m2m.out.fifo_in, out_buf);
			}
			break;
		}

		ret = decode_access_unit(data, in_buf->buffer, in_buf->bytesused, out_buf);
		in_buf->bytesused = 0;
		k_fifo_put(&data->m2m.in.fifo_out, in_buf);

		if (ret == 0) {
			k_fifo_put(&data->m2m.out.fifo_out, out_buf);
		} else {
			k_fifo_put(&data->m2m.out.fifo_in, out_buf);
			if (ret != -EAGAIN) {
				LOG_ERR("Decode failed: %d", ret);
			}
		}
	}

	k_mutex_unlock(&data->lock);
}

static int video_edge264_get_fmt(const struct device *dev, struct video_format *fmt)
{
	struct video_edge264_data *data = dev->data;

	*fmt = (fmt->type == VIDEO_BUF_TYPE_INPUT) ? data->m2m.in.fmt : data->m2m.out.fmt;
	return 0;
}

static int video_edge264_set_fmt(const struct device *dev, struct video_format *fmt)
{
	struct video_edge264_data *data = dev->data;
	struct video_common_header *common =
		(fmt->type == VIDEO_BUF_TYPE_INPUT) ? &data->m2m.in : &data->m2m.out;
	const struct video_format_cap *caps =
		(fmt->type == VIDEO_BUF_TYPE_INPUT) ? in_fmts : out_fmts;
	size_t idx;
	int ret;

	ret = video_format_caps_index(caps, fmt, &idx);
	if (ret != 0) {
		LOG_ERR("Unsupported pixel format or resolution");
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (common->is_streaming) {
		ret = -EBUSY;
		goto out;
	}

	ret = video_estimate_fmt_size(fmt);
	if (ret != 0) {
		goto out;
	}

	if (fmt->pixelformat == VIDEO_PIX_FMT_YUV420) {
		fmt->pitch = fmt->width;
		fmt->size = fmt->width * fmt->height * 3U / 2U;
	}

	common->fmt = *fmt;

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int video_edge264_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	struct video_edge264_data *data = dev->data;
	struct video_common_header *common =
		(type == VIDEO_BUF_TYPE_INPUT) ? &data->m2m.in : &data->m2m.out;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable == common->is_streaming) {
		ret = -EALREADY;
		goto out;
	}

	common->is_streaming = enable;

	if ((enable == false) && (data->dec != NULL)) {
		edge264_flush(data->dec);
	}

	if (enable && data->m2m.in.is_streaming && data->m2m.out.is_streaming) {
		k_work_submit_to_queue(&edge264_wq, &data->work);
	}

out:
	k_mutex_unlock(&data->lock);
	return ret;
}

static int video_edge264_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct video_edge264_data *data = dev->data;
	struct video_common_header *common =
		(vbuf->type == VIDEO_BUF_TYPE_INPUT) ? &data->m2m.in : &data->m2m.out;

	k_fifo_put(&common->fifo_in, vbuf);

	if (data->m2m.in.is_streaming && data->m2m.out.is_streaming) {
		k_work_submit_to_queue(&edge264_wq, &data->work);
	}

	return 0;
}

static int video_edge264_dequeue(const struct device *dev, struct video_buffer **vbuf,
				 k_timeout_t timeout)
{
	struct video_edge264_data *data = dev->data;
	struct video_common_header *common =
		((*vbuf)->type == VIDEO_BUF_TYPE_INPUT) ? &data->m2m.in : &data->m2m.out;

	*vbuf = k_fifo_get(&common->fifo_out, timeout);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	return 0;
}

static int video_edge264_get_caps(const struct device *dev, struct video_caps *caps)
{
	ARG_UNUSED(dev);

	caps->format_caps = (caps->type == VIDEO_BUF_TYPE_OUTPUT) ? out_fmts : in_fmts;
	caps->min_vbuf_count = 1;
	return 0;
}

static DEVICE_API(video, video_edge264_driver_api) = {
	.set_format = video_edge264_set_fmt,
	.get_format = video_edge264_get_fmt,
	.set_stream = video_edge264_set_stream,
	.enqueue = video_edge264_enqueue,
	.dequeue = video_edge264_dequeue,
	.get_caps = video_edge264_get_caps,
};

static int video_edge264_init(const struct device *dev)
{
	struct video_edge264_data *data = dev->data;

	data->dev = dev;
	k_mutex_init(&data->lock);
	k_fifo_init(&data->m2m.in.fifo_in);
	k_fifo_init(&data->m2m.in.fifo_out);
	k_fifo_init(&data->m2m.out.fifo_in);
	k_fifo_init(&data->m2m.out.fifo_out);
	k_work_init(&data->work, video_edge264_worker);
	edge264_wq_ensure_started();

	data->m2m.in.fmt.type = VIDEO_BUF_TYPE_INPUT;
	data->m2m.in.fmt.width = in_fmts[0].width_min;
	data->m2m.in.fmt.height = in_fmts[0].height_min;
	data->m2m.in.fmt.pixelformat = VIDEO_PIX_FMT_H264;

	data->m2m.out.fmt.type = VIDEO_BUF_TYPE_OUTPUT;
	data->m2m.out.fmt.width = out_fmts[0].width_min;
	data->m2m.out.fmt.height = out_fmts[0].height_min;
	data->m2m.out.fmt.pixelformat = VIDEO_PIX_FMT_RGB565;
	(void)video_estimate_fmt_size(&data->m2m.out.fmt);

	/* Single-threaded decode: worker_loop runs on the caller. */
	data->dec = edge264_alloc(0, NULL, NULL, 0, NULL, NULL, NULL);
	if (data->dec == NULL) {
		LOG_ERR("edge264_alloc failed");
		return -ENOMEM;
	}

	LOG_INF("%s initialized", dev->name);
	return 0;
}

#define VIDEO_EDGE264_DEFINE(n)                                                                   \
	static struct video_edge264_data video_edge264_data_##n;                                 \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, &video_edge264_init, NULL, &video_edge264_data_##n,               \
			      NULL, POST_KERNEL, CONFIG_VIDEO_INIT_PRIORITY,                     \
			      &video_edge264_driver_api);                                          \
                                                                                                   \
	VIDEO_DEVICE_DEFINE(video_edge264_##n, DEVICE_DT_INST_GET(n), NULL);

DT_INST_FOREACH_STATUS_OKAY(VIDEO_EDGE264_DEFINE)
