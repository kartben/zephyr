/*
 * Copyright (c) 2019 Linaro Limited
 * Copyright 2025 NXP
 * Copyright (c) 2025 STMicroelectronics.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/socket.h>

#include <zephyr/posix/netinet/in.h>
#include <zephyr/posix/sys/socket.h>
#include <zephyr/posix/arpa/inet.h>
#include <zephyr/posix/unistd.h>
#include <zephyr/posix/poll.h>

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

#define MY_PORT          5000
#define MAX_CLIENT_QUEUE 1

static ssize_t sendall(int sock, const void *buf, size_t len)
{
	while (len) {
		ssize_t out_len = send(sock, buf, len, 0);

		if (out_len < 0) {
			return out_len;
		}
		buf = (const char *)buf + out_len;
		len -= out_len;
	}

	return 0;
}

static int app_setup_network(int *sock)
{
	struct sockaddr_in addr = {
		.sin_family = AF_INET,
		.sin_port = htons(MY_PORT),
	};
	int ret;

	*sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (*sock < 0) {
		LOG_ERR("Failed to create TCP socket: %d", errno);
		return -errno;
	}

	ret = bind(*sock, (struct sockaddr *)&addr, sizeof(addr));
	if (ret < 0) {
		LOG_ERR("Failed to bind TCP socket: %d", errno);
		close(*sock);
		return -errno;
	}

	ret = listen(*sock, MAX_CLIENT_QUEUE);
	if (ret < 0) {
		LOG_ERR("Failed to listen on TCP socket: %d", errno);
		close(*sock);
		return -errno;
	}

	return 0;
}

static int app_query_video_info(const struct device *dev, struct video_format *fmt)
{
	struct video_caps caps = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	LOG_INF("Camera device: %s", dev->name);

	if (!device_is_ready(dev)) {
		LOG_ERR("%s: video device not ready.", dev->name);
		return -ENODEV;
	}

	/* Get capabilities */
	ret = video_get_caps(dev, &caps);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video capabilities");
		return ret;
	}

	LOG_INF("- Capabilities:");
	for (int i = 0; caps.format_caps[i].pixelformat; i++) {
		const struct video_format_cap *fcap = &caps.format_caps[i];

		LOG_INF("  %s width [%u; %u; %u] height [%u; %u; %u]",
			VIDEO_FOURCC_TO_STR(fcap->pixelformat),
			fcap->width_min, fcap->width_max, fcap->width_step,
			fcap->height_min, fcap->height_max, fcap->height_step);
	}

	/* Get default/native format */
	fmt->type = VIDEO_BUF_TYPE_OUTPUT;
	ret = video_get_format(dev, fmt);
	if (ret < 0) {
		LOG_ERR("Unable to retrieve video format");
		return ret;
	}

	LOG_INF("Video device detected, format: %s %ux%u",
		VIDEO_FOURCC_TO_STR(fmt->pixelformat), fmt->width, fmt->height);

	/* Adjust format according to configuration */
	if (CONFIG_VIDEO_FRAME_HEIGHT > 0) {
		fmt->height = CONFIG_VIDEO_FRAME_HEIGHT;
	}
	if (CONFIG_VIDEO_FRAME_WIDTH > 0) {
		fmt->width = CONFIG_VIDEO_FRAME_WIDTH;
	}
	if (strcmp(CONFIG_VIDEO_PIXEL_FORMAT, "") != 0) {
		fmt->pixelformat = VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_PIXEL_FORMAT);
	}

	return 0;
}

static int app_setup_video_selection(const struct device *dev)
{
	struct video_selection sel = {
		.type = VIDEO_BUF_TYPE_OUTPUT,
	};
	int ret;

	/* Set the crop setting only if configured */
	if (CONFIG_VIDEO_SOURCE_CROP_WIDTH > 0 && CONFIG_VIDEO_SOURCE_CROP_HEIGHT > 0) {
		sel.target = VIDEO_SEL_TGT_CROP;
		sel.rect.left = CONFIG_VIDEO_SOURCE_CROP_LEFT;
		sel.rect.top = CONFIG_VIDEO_SOURCE_CROP_TOP;
		sel.rect.width = CONFIG_VIDEO_SOURCE_CROP_WIDTH;
		sel.rect.height = CONFIG_VIDEO_SOURCE_CROP_HEIGHT;

		ret = video_set_selection(dev, &sel);
		if (ret < 0) {
			LOG_ERR("Unable to set selection crop");
			return ret;
		}

		LOG_INF("Crop set to (%u,%u)/%ux%u",
			sel.rect.left, sel.rect.top, sel.rect.width, sel.rect.height);
	}

	return 0;
}

static int app_setup_video_format(const struct device *dev, struct video_format *fmt)
{
	int ret;

	LOG_INF("- Video format: %s %ux%u",
		VIDEO_FOURCC_TO_STR(fmt->pixelformat), fmt->width, fmt->height);

	ret = video_set_compose_format(dev, fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set format");
		return ret;
	}

	return 0;
}

static int app_setup_video_frmival(const struct device *dev, struct video_format *fmt)
{
	struct video_frmival frmival = {};
	struct video_frmival_enum fie = {
		.format = fmt,
	};

	if (video_get_frmival(dev, &frmival) == 0) {
		LOG_INF("- Default frame rate : %f fps",
			1.0 * frmival.denominator / frmival.numerator);
	}

	LOG_INF("- Supported frame intervals for the default format:");
	while (video_enum_frmival(dev, &fie) == 0) {
		if (fie.type == VIDEO_FRMIVAL_TYPE_DISCRETE) {
			LOG_INF("   %u/%u", fie.discrete.numerator, fie.discrete.denominator);
		} else {
			LOG_INF("   [min = %u/%u; max = %u/%u; step = %u/%u]",
				fie.stepwise.min.numerator, fie.stepwise.min.denominator,
				fie.stepwise.max.numerator, fie.stepwise.max.denominator,
				fie.stepwise.step.numerator, fie.stepwise.step.denominator);
		}
		fie.index++;
	}

	return 0;
}

static int app_setup_video_controls(const struct device *dev)
{
	const struct device *last_dev = NULL;
	struct video_ctrl_query cq = {.dev = dev, .id = VIDEO_CTRL_FLAG_NEXT_CTRL};
	struct video_control ctrl = {.id = VIDEO_CID_HFLIP, .val = 1};
	int ret;

	/* Get supported controls */
	LOG_INF("- Supported controls:");
	while (video_query_ctrl(&cq) == 0) {
		if (cq.dev != last_dev) {
			last_dev = cq.dev;
			LOG_INF("\t\tdevice: %s", cq.dev->name);
		}
		video_print_ctrl(&cq);
		cq.id |= VIDEO_CTRL_FLAG_NEXT_CTRL;
	}

	/* Set controls */
	if (IS_ENABLED(CONFIG_VIDEO_CTRL_HFLIP)) {
		video_set_ctrl(dev, &ctrl);
	}

	if (IS_ENABLED(CONFIG_VIDEO_CTRL_VFLIP)) {
		ctrl.id = VIDEO_CID_VFLIP;
		video_set_ctrl(dev, &ctrl);
	}

	if (IS_ENABLED(CONFIG_TEST)) {
		ctrl.id = VIDEO_CID_TEST_PATTERN;
		ret = video_set_ctrl(dev, &ctrl);
		if (ret < 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set the test pattern");
		}
	}

	return 0;
}

#if DT_HAS_CHOSEN(zephyr_videoenc)
static const struct device *encoder_dev;

static int configure_encoder(const struct video_format *camera_fmt)
{
	struct video_buffer *buffer;
	struct video_format fmt = *camera_fmt;
	int ret;

	encoder_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_videoenc));
	if (!device_is_ready(encoder_dev)) {
		LOG_ERR("%s: encoder device not ready.", encoder_dev->name);
		return -ENODEV;
	}

	/* Set output format (compressed) */
	if (strcmp(CONFIG_VIDEO_ENCODED_PIXEL_FORMAT, "") != 0) {
		fmt.pixelformat = VIDEO_FOURCC_FROM_STR(CONFIG_VIDEO_ENCODED_PIXEL_FORMAT);
	}
	fmt.type = VIDEO_BUF_TYPE_OUTPUT;

	LOG_INF("- Encoder output format: %s %ux%u",
		VIDEO_FOURCC_TO_STR(fmt.pixelformat), fmt.width, fmt.height);

	ret = video_set_format(encoder_dev, &fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set encoder output format");
		return ret;
	}

	/* Alloc output buffer for compressed data */
	if (fmt.size == 0) {
		LOG_ERR("Encoder driver must set format size");
		return -EINVAL;
	}

	buffer = video_buffer_aligned_alloc(fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
	if (buffer == NULL) {
		LOG_ERR("Unable to alloc compressed video buffer (size=%u)", fmt.size);
		return -ENOMEM;
	}

	buffer->type = VIDEO_BUF_TYPE_OUTPUT;
	ret = video_enqueue(encoder_dev, buffer);
	if (ret < 0) {
		LOG_ERR("Unable to enqueue encoder output buffer");
		return ret;
	}

	/* Set input format (raw) */
	fmt.pixelformat = camera_fmt->pixelformat;
	fmt.type = VIDEO_BUF_TYPE_INPUT;

	LOG_INF("- Encoder input format: %s %ux%u",
		VIDEO_FOURCC_TO_STR(fmt.pixelformat), fmt.width, fmt.height);

	ret = video_set_format(encoder_dev, &fmt);
	if (ret < 0) {
		LOG_ERR("Unable to set encoder input format");
		return ret;
	}

	/* Start encoder */
	ret = video_stream_start(encoder_dev, VIDEO_BUF_TYPE_INPUT);
	if (ret < 0) {
		LOG_ERR("Unable to start encoder (input)");
		return ret;
	}

	ret = video_stream_start(encoder_dev, VIDEO_BUF_TYPE_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Unable to start encoder (output)");
		return ret;
	}

	return 0;
}

static int encode_frame(struct video_buffer *in, struct video_buffer **out)
{
	int ret;

	in->type = VIDEO_BUF_TYPE_INPUT;
	ret = video_enqueue(encoder_dev, in);
	if (ret < 0) {
		LOG_ERR("Unable to enqueue encoder input buffer");
		return ret;
	}

	(*out)->type = VIDEO_BUF_TYPE_OUTPUT;
	ret = video_dequeue(encoder_dev, out, K_FOREVER);
	if (ret < 0) {
		LOG_ERR("Unable to dequeue encoder output buffer");
		return ret;
	}

	ret = video_dequeue(encoder_dev, &in, K_FOREVER);
	if (ret < 0) {
		LOG_ERR("Unable to dequeue encoder input buffer");
		return ret;
	}

	return 0;
}

static void stop_encoder(void)
{
	if (video_stream_stop(encoder_dev, VIDEO_BUF_TYPE_OUTPUT)) {
		LOG_ERR("Unable to stop encoder (output)");
	}

	if (video_stream_stop(encoder_dev, VIDEO_BUF_TYPE_INPUT)) {
		LOG_ERR("Unable to stop encoder (input)");
	}
}
#endif

int main(void)
{
	const struct device *const video_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_camera));
	struct video_buffer *buffers[CONFIG_VIDEO_CAPTURE_N_BUFFERING];
	struct video_buffer *vbuf = &(struct video_buffer){};
#if DT_HAS_CHOSEN(zephyr_videoenc)
	struct video_buffer *vbuf_out = &(struct video_buffer){};
#endif
	struct video_format fmt = {};
	int ret, sock, client;

	ret = app_query_video_info(video_dev, &fmt);
	if (ret < 0) {
		goto err;
	}

	ret = app_setup_network(&sock);
	if (ret < 0) {
		goto err;
	}

	ret = app_setup_video_selection(video_dev);
	if (ret < 0) {
		goto err;
	}

	ret = app_setup_video_format(video_dev, &fmt);
	if (ret < 0) {
		goto err;
	}

	app_setup_video_frmival(video_dev, &fmt);
	app_setup_video_controls(video_dev);

	/* Alloc video buffers */
	for (int i = 0; i < ARRAY_SIZE(buffers); i++) {
		buffers[i] = video_buffer_aligned_alloc(fmt.size, CONFIG_VIDEO_BUFFER_POOL_ALIGN,
							K_NO_WAIT);
		if (buffers[i] == NULL) {
			LOG_ERR("Unable to alloc video buffer");
			ret = -ENOMEM;
			goto err;
		}
		buffers[i]->type = VIDEO_BUF_TYPE_OUTPUT;
	}

	/* Connection loop */
	do {
		bool disconnected = false;
		unsigned int frame = 0;

		LOG_INF("TCP: Waiting for client...");

		client = accept(sock, NULL, NULL);
		if (client < 0) {
			LOG_ERR("Failed to accept: %d", errno);
			ret = -errno;
			goto err;
		}

		LOG_INF("TCP: Accepted connection");

#if DT_HAS_CHOSEN(zephyr_videoenc)
		ret = configure_encoder(&fmt);
		if (ret < 0) {
			LOG_ERR("Unable to configure encoder");
			goto err;
		}
#endif

		/* Enqueue buffers */
		for (int i = 0; i < ARRAY_SIZE(buffers); i++) {
			ret = video_enqueue(video_dev, buffers[i]);
			if (ret < 0) {
				LOG_ERR("Unable to enqueue video buffer");
				goto err;
			}
		}

		/* Start video capture */
		ret = video_stream_start(video_dev, VIDEO_BUF_TYPE_OUTPUT);
		if (ret < 0) {
			LOG_ERR("Unable to start video");
			goto err;
		}

		LOG_INF("Stream started");

		/* Capture loop */
		vbuf->type = VIDEO_BUF_TYPE_OUTPUT;
		do {
			ret = video_dequeue(video_dev, &vbuf, K_FOREVER);
			if (ret < 0) {
				LOG_ERR("Unable to dequeue video buffer");
				goto err;
			}

#if DT_HAS_CHOSEN(zephyr_videoenc)
			ret = encode_frame(vbuf, &vbuf_out);
			if (ret < 0) {
				LOG_ERR("Unable to encode frame");
				goto err;
			}

			vbuf->type = VIDEO_BUF_TYPE_INPUT;
			ret = video_enqueue(video_dev, vbuf);
			if (ret < 0) {
				LOG_ERR("Unable to re-enqueue video buffer");
				goto err;
			}

			LOG_INF("Sending compressed frame %u (size=%u bytes)",
				frame++, vbuf_out->bytesused);
			ret = sendall(client, vbuf_out->buffer, vbuf_out->bytesused);
			disconnected = ret && ret != -EAGAIN;

			vbuf_out->type = VIDEO_BUF_TYPE_OUTPUT;
			ret = video_enqueue(encoder_dev, vbuf_out);
			if (ret < 0) {
				LOG_ERR("Unable to re-enqueue encoder output buffer");
				goto err;
			}
#else
			LOG_INF("Sending frame %u", frame++);
			ret = sendall(client, vbuf->buffer, vbuf->bytesused);
			disconnected = ret && ret != -EAGAIN;

			vbuf->type = VIDEO_BUF_TYPE_INPUT;
			ret = video_enqueue(video_dev, vbuf);
			if (ret < 0) {
				LOG_ERR("Unable to re-enqueue video buffer");
				goto err;
			}
#endif
			if (disconnected) {
				LOG_ERR("TCP: Client disconnected");
				close(client);
			}

		} while (!ret && !disconnected);

		/* Stop capture */
		if (video_stream_stop(video_dev, VIDEO_BUF_TYPE_OUTPUT)) {
			LOG_ERR("Unable to stop video");
		}

#if DT_HAS_CHOSEN(zephyr_videoenc)
		stop_encoder();
#endif

		/* Flush remaining buffers */
		do {
			vbuf->type = VIDEO_BUF_TYPE_INPUT;
			ret = video_dequeue(video_dev, &vbuf, K_NO_WAIT);
		} while (!ret);

	} while (1);

err:
	LOG_ERR("Aborting");
	return 0;
}
