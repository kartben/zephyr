/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <sample_usbd.h>

#include <zephyr/device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/class/usbd_uvc.h>
#include <zephyr/kernel.h>

LOG_MODULE_REGISTER(uvc_sample, LOG_LEVEL_INF);

const static struct device *const uvc_dev = DEVICE_DT_GET(DT_NODELABEL(uvc));
const static struct device *const video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));

/* Format capabilities of video_dev, usd everywhere through the sampel */
static struct video_caps video_caps = {.type = VIDEO_BUF_TYPE_OUTPUT};

/* Simple 8x8 bitmap font for digits 0-9 and "." */
#define FONT_WIDTH 8
#define FONT_HEIGHT 8

static const uint8_t font_data[][FONT_HEIGHT] = {
	/* '0' */ {0x3C, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x3C},
	/* '1' */ {0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x18, 0x7E},
	/* '2' */ {0x3C, 0x66, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x7E},
	/* '3' */ {0x3C, 0x66, 0x06, 0x1C, 0x06, 0x06, 0x66, 0x3C},
	/* '4' */ {0x0C, 0x1C, 0x2C, 0x4C, 0x7E, 0x0C, 0x0C, 0x0C},
	/* '5' */ {0x7E, 0x60, 0x60, 0x7C, 0x06, 0x06, 0x66, 0x3C},
	/* '6' */ {0x3C, 0x66, 0x60, 0x7C, 0x66, 0x66, 0x66, 0x3C},
	/* '7' */ {0x7E, 0x06, 0x0C, 0x18, 0x30, 0x30, 0x30, 0x30},
	/* '8' */ {0x3C, 0x66, 0x66, 0x3C, 0x66, 0x66, 0x66, 0x3C},
	/* '9' */ {0x3C, 0x66, 0x66, 0x66, 0x3E, 0x06, 0x66, 0x3C},
	/* '.' */ {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18},
};

static int get_char_index(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c == '.') {
		return 10;
	}
	return -1; /* Unsupported character */
}

static void draw_char_grey(uint8_t *buffer, int buf_width, int buf_height,
			   int x, int y, char c)
{
	int char_idx = get_char_index(c);
	if (char_idx < 0) {
		return;
	}

	const uint8_t *char_data = font_data[char_idx];

	for (int row = 0; row < FONT_HEIGHT; row++) {
		if (y + row >= buf_height) break;

		uint8_t pixel_row = char_data[row];
		for (int col = 0; col < FONT_WIDTH; col++) {
			if (x + col >= buf_width) break;

			if (pixel_row & (0x80 >> col)) {
				/* Set pixel to white (255) */
				buffer[(y + row) * buf_width + (x + col)] = 255;
			}
		}
	}
}

static void draw_char_yuyv(uint8_t *buffer, int buf_width, int buf_height,
			   int x, int y, char c)
{
	int char_idx = get_char_index(c);
	if (char_idx < 0) {
		return;
	}

	const uint8_t *char_data = font_data[char_idx];

	for (int row = 0; row < FONT_HEIGHT; row++) {
		if (y + row >= buf_height) break;

		uint8_t pixel_row = char_data[row];
		for (int col = 0; col < FONT_WIDTH; col++) {
			if (x + col >= buf_width) break;

			if (pixel_row & (0x80 >> col)) {
				/* YUYV format: Y0 U Y1 V (2 pixels per 4 bytes) */
				int pixel_idx = (y + row) * buf_width + (x + col);
				int byte_idx = (pixel_idx / 2) * 4 + (pixel_idx % 2) * 2;

				if (byte_idx < buf_width * buf_height * 2) {
					buffer[byte_idx] = 255; /* Set Y (luminance) to white */
				}
			}
		}
	}
}

static void draw_text_overlay(struct video_buffer *vbuf, const struct video_format *fmt,
			      const char *text, int x, int y)
{
	int text_len = strlen(text);

	for (int i = 0; i < text_len; i++) {
		int char_x = x + i * (FONT_WIDTH + 2); /* 2 pixel spacing between chars */

		switch (fmt->pixelformat) {
		case VIDEO_PIX_FMT_GREY:
			draw_char_grey(vbuf->buffer, fmt->width, fmt->height,
				       char_x, y, text[i]);
			break;
		case VIDEO_PIX_FMT_YUYV:
			draw_char_yuyv(vbuf->buffer, fmt->width, fmt->height,
				       char_x, y, text[i]);
			break;
		default:
			/* Skip overlay for unsupported formats like JPEG */
			break;
		}
	}
}

static void format_uptime_string(char *buffer, size_t buffer_size)
{
	int64_t uptime_ms = k_uptime_get();
	int64_t seconds = uptime_ms / 1000;
	int64_t milliseconds = uptime_ms % 1000;

	/* Format as seconds with 3 decimal places: "123.456" */
	snprintf(buffer, buffer_size, "%lld.%03lld", seconds, milliseconds);
}

static size_t app_get_min_buf_size(const struct video_format *const fmt)
{
	if (video_caps.min_line_count == LINE_COUNT_HEIGHT) {
		return fmt->pitch * fmt->height;
	} else {
		return fmt->pitch * video_caps.min_line_count;
	}
}

static bool app_is_standard_format(uint32_t pixfmt)
{
	return pixfmt == VIDEO_PIX_FMT_GREY || pixfmt == VIDEO_PIX_FMT_JPEG ||
	       pixfmt == VIDEO_PIX_FMT_YUYV;
}

/* Check whether the video device supports one of the wisespread image sensor formats */
static bool app_has_standard_formats(void)
{
	for (int i = 0;; i++) {
		uint32_t pixfmt = video_caps.format_caps[i].pixelformat;

		if (pixfmt == 0) {
			return false;
		}
		if (app_is_standard_format(pixfmt)) {
			return true;
		}
	}
}

static void app_add_format(uint32_t pixfmt, uint16_t width, uint16_t height, bool has_std_fmts)
{
	struct video_format fmt = {
		.pixelformat = pixfmt,
		.width = width,
		.height = height,
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	/* If the system has any standard pixel format, only propose them to the host */
	if (has_std_fmts && !app_is_standard_format(pixfmt)) {
		return;
	}

	/* Set the format to get the pitch */
	ret = video_set_format(video_dev, &fmt);
	if (ret != 0) {
		LOG_ERR("Could not set the format of %s", video_dev->name);
		return;
	}

	if (app_get_min_buf_size(&fmt) > CONFIG_VIDEO_BUFFER_POOL_SZ_MAX) {
		LOG_WRN("Skipping format %ux%u", fmt.width, fmt.height);
		return;
	}

	uvc_add_format(uvc_dev, &fmt);
}

/* Submit to UVC only the formats expected to be working (enough memory for the size, etc.) */
static void app_add_filtered_formats(void)
{
	const bool has_std_fmts = app_has_standard_formats();

	for (int i = 0; video_caps.format_caps[i].pixelformat != 0; i++) {
		const struct video_format_cap *vcap = &video_caps.format_caps[i];

		app_add_format(vcap->pixelformat, vcap->width_min, vcap->height_min, has_std_fmts);

		if (vcap->width_min != vcap->width_max || vcap->height_min != vcap->height_max) {
			app_add_format(vcap->pixelformat, vcap->width_max, vcap->height_max,
				       has_std_fmts);
		}
	}
}

int main(void)
{
	struct usbd_context *sample_usbd;
	struct video_buffer *vbuf;
	struct video_format fmt = {0};
	struct video_format current_fmt = {0}; /* Store current format for overlay */
	struct video_frmival frmival = {0};
	struct k_poll_signal sig;
	struct k_poll_event evt[1];
	k_timeout_t timeout = K_FOREVER;
	size_t bsize;
	int ret;

	if (!device_is_ready(video_dev)) {
		LOG_ERR("video source %s failed to initialize", video_dev->name);
		return -ENODEV;
	}

	ret = video_get_caps(video_dev, &video_caps);
	if (ret != 0) {
		LOG_ERR("Unable to retrieve video capabilities");
		return 0;
	}

	/* Must be called before usb_enable() */
	uvc_set_video_dev(uvc_dev, video_dev);

	/* Must be called before uvc_set_video_dev() */
	app_add_filtered_formats();

	sample_usbd = sample_usbd_init_device(NULL);
	if (sample_usbd == NULL) {
		return -ENODEV;
	}

	ret = usbd_enable(sample_usbd);
	if (ret != 0) {
		return ret;
	}

	LOG_INF("Waiting the host to select the video format");

	while (true) {
		fmt.type = VIDEO_BUF_TYPE_INPUT;

		ret = video_get_format(uvc_dev, &fmt);
		if (ret == 0) {
			break;
		}
		if (ret != -EAGAIN) {
			LOG_ERR("Failed to get the video format");
			return ret;
		}

		k_sleep(K_MSEC(10));
	}

	ret = video_get_frmival(uvc_dev, &frmival);
	if (ret != 0) {
		LOG_ERR("Failed to get the video frame interval");
		return ret;
	}

	LOG_INF("The host selected format '%s' %ux%u at frame interval %u/%u",
		VIDEO_FOURCC_TO_STR(fmt.pixelformat), fmt.width, fmt.height,
		frmival.numerator, frmival.denominator);

	fmt.type = VIDEO_BUF_TYPE_OUTPUT;

	ret = video_set_format(video_dev, &fmt);
	if (ret != 0) {
		LOG_WRN("Could not set the format of %s", video_dev->name);
	}

	/* Store the current format for text overlay */
	current_fmt = fmt;

	ret = video_set_frmival(video_dev, &frmival);
	if (ret != 0) {
		LOG_WRN("Could not set the framerate of %s", video_dev->name);
	}

	bsize = app_get_min_buf_size(&fmt);

	LOG_INF("Preparing %u buffers of %u bytes", CONFIG_VIDEO_BUFFER_POOL_NUM_MAX, bsize);

	for (int i = 0; i < CONFIG_VIDEO_BUFFER_POOL_NUM_MAX; i++) {
		vbuf = video_buffer_alloc(bsize, K_NO_WAIT);
		if (vbuf == NULL) {
			LOG_ERR("Could not allocate the video buffer");
			return -ENOMEM;
		}

		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;

		ret = video_enqueue(video_dev, vbuf);
		if (ret != 0) {
			LOG_ERR("Could not enqueue video buffer");
			return ret;
		}
	}

	LOG_DBG("Preparing signaling for %s input/output", video_dev->name);

	k_poll_signal_init(&sig);
	k_poll_event_init(&evt[0], K_POLL_TYPE_SIGNAL, K_POLL_MODE_NOTIFY_ONLY, &sig);

	ret = video_set_signal(video_dev, &sig);
	if (ret != 0) {
		LOG_WRN("Failed to setup the signal on %s output endpoint", video_dev->name);
		timeout = K_MSEC(1);
	}

	ret = video_set_signal(uvc_dev, &sig);
	if (ret != 0) {
		LOG_ERR("Failed to setup the signal on %s input endpoint", uvc_dev->name);
		return ret;
	}

	LOG_INF("Starting the video transfer");

	ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret != 0) {
		LOG_ERR("Failed to start %s", video_dev->name);
		return ret;
	}

	while (true) {
		ret = k_poll(evt, ARRAY_SIZE(evt), timeout);
		if (ret != 0 && ret != -EAGAIN) {
			LOG_ERR("Poll exited with status %d", ret);
			return ret;
		}

		vbuf = &(struct video_buffer){.type = VIDEO_BUF_TYPE_OUTPUT};

		if (video_dequeue(video_dev, &vbuf, K_NO_WAIT) == 0) {
			LOG_DBG("Dequeued %p from %s, enqueueing to %s",
				(void *)vbuf, video_dev->name, uvc_dev->name);

			/* Format current uptime and add as text overlay */
			char uptime_text[32];
			format_uptime_string(uptime_text, sizeof(uptime_text));
			draw_text_overlay(vbuf, &current_fmt, uptime_text, 10, 10);

			vbuf->type = VIDEO_BUF_TYPE_INPUT;

			ret = video_enqueue(uvc_dev, vbuf);
			if (ret != 0) {
				LOG_ERR("Could not enqueue video buffer to %s", uvc_dev->name);
				return ret;
			}
		}

		vbuf = &(struct video_buffer){.type = VIDEO_BUF_TYPE_INPUT};

		if (video_dequeue(uvc_dev, &vbuf, K_NO_WAIT) == 0) {
			LOG_DBG("Dequeued %p from %s, enqueueing to %s",
				(void *)vbuf, uvc_dev->name, video_dev->name);

			vbuf->type = VIDEO_BUF_TYPE_OUTPUT;

			ret = video_enqueue(video_dev, vbuf);
			if (ret != 0) {
				LOG_ERR("Could not enqueue video buffer to %s", video_dev->name);
				return ret;
			}
		}

		k_poll_signal_reset(&sig);
	}

	return 0;
}
