/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/video/video.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

static const uint8_t h264_clip[] = {
#include "testsrc_160x120.h264.inc"
};

#if !DT_HAS_CHOSEN(zephyr_videodec)
#error No H.264 decoder in devicetree. Missing board overlay?
#endif

static int setup_display(const struct device *display_dev)
{
	struct display_capabilities caps;
	int ret;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("Display not ready");
		return -ENODEV;
	}

	display_get_capabilities(display_dev, &caps);
	if (caps.current_pixel_format != PIXEL_FORMAT_RGB_565) {
		ret = display_set_pixel_format(display_dev, PIXEL_FORMAT_RGB_565);
		if (ret != 0) {
			LOG_WRN("RGB565 not supported on the display (%d)", ret);
			return ret;
		}
	}

	ret = display_blanking_off(display_dev);
	if ((ret != 0) && (ret != -ENOSYS)) {
		return ret;
	}

	return 0;
}

static int show_frame(const struct device *display_dev, const struct video_buffer *vbuf,
		      const struct video_format *fmt)
{
	struct display_buffer_descriptor desc = {
		.buf_size = vbuf->bytesused,
		.width = fmt->width,
		.pitch = fmt->width,
		.height = fmt->height,
	};

	return display_write(display_dev, 0, 0, &desc, vbuf->buffer);
}

int main(void)
{
	const struct device *dec_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_videodec));
#if defined(CONFIG_DISPLAY) && DT_HAS_CHOSEN(zephyr_display)
	const struct device *display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
#else
	const struct device *display_dev = NULL;
#endif
	struct video_format in_fmt = {
		.type = VIDEO_BUF_TYPE_INPUT,
		.pixelformat = VIDEO_PIX_FMT_H264,
		.width = 160,
		.height = 120,
	};
	struct video_format out_fmt = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
		.pixelformat = VIDEO_PIX_FMT_RGB565,
		.width = 160,
		.height = 120,
	};
	struct video_buffer *in_buf;
	struct video_buffer *out_buf;
	struct video_buffer *done;
	int ret;
	int frame = 0;

	if (!device_is_ready(dec_dev)) {
		LOG_ERR("Decoder device is not ready");
		return 0;
	}

	LOG_INF("Decoder device: %s", dec_dev->name);

	ret = video_estimate_fmt_size(&in_fmt);
	if (ret != 0) {
		LOG_ERR("Cannot estimate input format size");
		return 0;
	}
	in_fmt.size = sizeof(h264_clip);

	ret = video_set_format(dec_dev, &in_fmt);
	if (ret != 0) {
		LOG_ERR("Cannot set decoder input format");
		return 0;
	}

	ret = video_set_format(dec_dev, &out_fmt);
	if (ret != 0) {
		LOG_ERR("Cannot set decoder output format");
		return 0;
	}

	in_buf = video_buffer_aligned_alloc(in_fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
	out_buf = video_buffer_aligned_alloc(out_fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN,
					      K_NO_WAIT);
	if ((in_buf == NULL) || (out_buf == NULL)) {
		LOG_ERR("Cannot allocate video buffers");
		return 0;
	}

	memcpy(in_buf->buffer, h264_clip, sizeof(h264_clip));
	in_buf->bytesused = sizeof(h264_clip);
	in_buf->type = VIDEO_BUF_TYPE_INPUT;
	out_buf->type = VIDEO_BUF_TYPE_OUTPUT;

	if (display_dev != NULL) {
		if (setup_display(display_dev) != 0) {
			display_dev = NULL;
		}
	}

	ret = video_stream_start(dec_dev, VIDEO_BUF_TYPE_INPUT);
	if (ret != 0) {
		LOG_ERR("Cannot start decoder input");
		return 0;
	}
	ret = video_stream_start(dec_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Cannot start decoder output");
		return 0;
	}

	while (1) {
		int64_t t0 = k_uptime_get();

		in_buf->bytesused = sizeof(h264_clip);
		in_buf->type = VIDEO_BUF_TYPE_INPUT;
		out_buf->type = VIDEO_BUF_TYPE_OUTPUT;

		ret = video_enqueue(dec_dev, in_buf);
		if (ret != 0) {
			LOG_ERR("Cannot enqueue input");
			return 0;
		}
		ret = video_enqueue(dec_dev, out_buf);
		if (ret != 0) {
			LOG_ERR("Cannot enqueue output");
			return 0;
		}

		done = in_buf;
		done->type = VIDEO_BUF_TYPE_INPUT;
		ret = video_dequeue(dec_dev, &done, K_SECONDS(5));
		if (ret != 0) {
			LOG_ERR("Input dequeue failed");
			return 0;
		}
		in_buf = done;

		done = out_buf;
		done->type = VIDEO_BUF_TYPE_OUTPUT;
		ret = video_dequeue(dec_dev, &done, K_SECONDS(5));
		if (ret != 0) {
			LOG_ERR("Output dequeue failed");
			return 0;
		}
		out_buf = done;

		LOG_INF("Decoded frame %d: %ux%u %s %u bytes in %lld ms", frame, out_fmt.width,
			out_fmt.height, VIDEO_FOURCC_TO_STR(out_fmt.pixelformat),
			out_buf->bytesused, k_uptime_get() - t0);

		if (display_dev != NULL) {
			ret = show_frame(display_dev, out_buf, &out_fmt);
			if (ret != 0) {
				LOG_WRN("Display write failed: %d", ret);
			}
		}

		frame++;
		k_sleep(K_MSEC(200));
	}

	return 0;
}
