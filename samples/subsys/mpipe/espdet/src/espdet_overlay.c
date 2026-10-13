/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/cache.h>
#include <zephyr/drivers/video.h>
#include <zephyr/logging/log.h>
#include <zephyr/mpipe/mpipe_dispatch.h>
#include <zephyr/mpipe/mpipe_pad.h>
#include <zephyr/mpipe/mpipe_structure.h>
#include <zephyr/mpipe/mpipe_value.h>

#include "espdet_overlay.h"
#include "overlay.h"

LOG_MODULE_REGISTER(espdet_overlay, CONFIG_LOG_DEFAULT_LEVEL);

#define FRAME_SIZE_MIN 16
#define FRAME_SIZE_MAX 1280

#define LABEL_SCALE   2
#define LABEL_PAD     2
#define LABEL_HEIGHT  (OVERLAY_FONT_HEIGHT * LABEL_SCALE + 2 * LABEL_PAD)
#define BOX_THICKNESS 2
#define MAX_CLASSES   8
#define MAX_LABEL_LEN 16

#define COLOR_BLACK OVERLAY_RGB(0, 0, 0)
#define COLOR_WHITE OVERLAY_RGB(255, 255, 255)

static const uint16_t class_colors[MAX_CLASSES] = {
	OVERLAY_RGB(0, 230, 118),  OVERLAY_RGB(255, 145, 0),  OVERLAY_RGB(41, 182, 246),
	OVERLAY_RGB(255, 64, 129), OVERLAY_RGB(255, 235, 59), OVERLAY_RGB(179, 136, 255),
	OVERLAY_RGB(0, 229, 255),  OVERLAY_RGB(255, 82, 82),
};

static char class_labels[MAX_CLASSES][MAX_LABEL_LEN];
static size_t class_count;

K_THREAD_STACK_DEFINE(espdet_stack, CONFIG_ESPDET_THREAD_STACK_SIZE);
static struct k_thread espdet_thread;

/* Split the comma-separated class list of the model */
static void parse_class_labels(void)
{
	const char *p = CONFIG_ESPDET_CLASS_LABELS;

	while (*p != '\0' && class_count < MAX_CLASSES) {
		const char *end = strchr(p, ',');
		size_t len = (end != NULL) ? (size_t)(end - p) : strlen(p);

		snprintf(class_labels[class_count], MAX_LABEL_LEN, "%.*s", (int)len, p);
		class_count++;

		if (end == NULL) {
			break;
		}
		p = end + 1;
	}
}

static void espdet_thread_fn(void *p1, void *p2, void *p3)
{
	struct espdet_overlay *self = p1;
	struct detector_box boxes[CONFIG_ESPDET_MAX_BOXES];
	int64_t start;
	int ret;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	self->init_err = detector_init();
	if (self->init_err != 0) {
		/* Leave busy set: no frame is ever handed over */
		return;
	}

	atomic_clear(&self->busy);

	while (true) {
		k_sem_take(&self->frame_ready, K_FOREVER);

		start = k_uptime_get();
		ret = detector_run(self->snapshot->buffer, self->width, self->height,
				   self->big_endian, boxes, ARRAY_SIZE(boxes));

		k_mutex_lock(&self->lock, K_FOREVER);
		self->box_count = MAX(ret, 0);
		memcpy(self->boxes, boxes, self->box_count * sizeof(boxes[0]));
		self->infer_ms = (uint32_t)(k_uptime_get() - start);
		self->infer_count++;
		k_mutex_unlock(&self->lock);

		if (IS_ENABLED(CONFIG_ESPDET_LOG_DETECTIONS)) {
			for (int i = 0; i < ret; i++) {
				LOG_INF("%s %u%% (%d,%d)-(%d,%d)",
					boxes[i].category < class_count
						? class_labels[boxes[i].category]
						: "?",
					boxes[i].score, boxes[i].x0, boxes[i].y0, boxes[i].x1,
					boxes[i].y1);
			}
		}

		atomic_clear(&self->busy);
	}
}

static void draw_detection(const struct overlay_frame *frame, const struct detector_box *box)
{
	uint16_t color = class_colors[box->category % MAX_CLASSES];
	char label[MAX_LABEL_LEN + 8];
	int label_y;
	int label_w;

	snprintf(label, sizeof(label), "%s %u%%",
		 box->category < class_count ? class_labels[box->category] : "?", box->score);
	label_w = overlay_text_width(label, LABEL_SCALE) + 2 * LABEL_PAD;

	overlay_rect(frame, box->x0, box->y0, box->x1, box->y1, BOX_THICKNESS, color);

	/* Label above the box, or inside it when the box touches the top of the frame */
	label_y = (box->y0 >= LABEL_HEIGHT) ? box->y0 - LABEL_HEIGHT : box->y0;
	overlay_fill(frame, box->x0, label_y, box->x0 + label_w - 1, label_y + LABEL_HEIGHT - 1,
		     color);
	overlay_text(frame, box->x0 + LABEL_PAD, label_y + LABEL_PAD, label, LABEL_SCALE,
		     COLOR_BLACK);
}

static void draw_status(struct espdet_overlay *self, const struct overlay_frame *frame,
			uint32_t infer_ms)
{
	char status[64];
	int y = frame->height - OVERLAY_FONT_HEIGHT - 2 * LABEL_PAD;

	if (self->init_err != 0) {
		snprintf(status, sizeof(status), "model load failed (%d)", self->init_err);
	} else if (infer_ms == 0U) {
		snprintf(status, sizeof(status), "loading model...");
	} else {
		snprintf(status, sizeof(status), "ESPDet-Pico %u ms | video %u.%u fps", infer_ms,
			 self->fps_x10 / 10U, self->fps_x10 % 10U);
	}

	overlay_fill(frame, 0, y, overlay_text_width(status, 1) + 2 * LABEL_PAD - 1,
		     frame->height - 1, COLOR_BLACK);
	overlay_text(frame, LABEL_PAD, y + LABEL_PAD, status, 1, COLOR_WHITE);
}

static void update_fps(struct espdet_overlay *self)
{
	int64_t now = k_uptime_get();

	self->frame_count++;
	if (now - self->fps_start >= MSEC_PER_SEC) {
		self->fps_x10 = (uint32_t)(self->frame_count * 10000U / (now - self->fps_start));
		self->frame_count = 0;
		self->fps_start = now;
	}
}

static int espdet_overlay_chain(struct mpipe_pad *pad, struct net_buf *buf,
				struct net_buf **out_buf)
{
	struct espdet_overlay *self = (struct espdet_overlay *)pad->object.container;
	size_t frame_size = self->pitch * self->height;
	struct detector_box boxes[CONFIG_ESPDET_MAX_BOXES];
	struct overlay_frame frame;
	uint32_t infer_ms;
	size_t count;

	/* Drawing happens in place: the same buffer goes downstream */
	*out_buf = buf;

	if (buf == NULL) {
		return 0;
	}

	/* Only whole frames in one piece can be analyzed and drawn on */
	if (buf->frags != NULL || mpipe_buffer_get_meta(buf)->bytes_used < frame_size ||
	    self->snapshot == NULL) {
		return 0;
	}

	/* The camera wrote the frame by DMA: drop what the cache holds of the last one */
	(void)sys_cache_data_invd_range(buf->data, frame_size);

	/* Hand the frame over only when the detector is idle, and before drawing on it */
	if (atomic_cas(&self->busy, 0, 1)) {
		memcpy(self->snapshot->buffer, buf->data, frame_size);
		k_sem_give(&self->frame_ready);
	}

	update_fps(self);

	k_mutex_lock(&self->lock, K_FOREVER);
	count = self->box_count;
	memcpy(boxes, self->boxes, count * sizeof(boxes[0]));
	infer_ms = self->infer_ms;
	k_mutex_unlock(&self->lock);

	frame = (struct overlay_frame){
		.buf = buf->data,
		.width = self->width,
		.height = self->height,
		.pitch = self->pitch,
		.big_endian = self->big_endian,
	};

	for (size_t i = 0; i < count; i++) {
		draw_detection(&frame, &boxes[i]);
	}

	draw_status(self, &frame, infer_ms);

	return 0;
}

/* RGB565 in either byte order, which is what the detector and the overlay handle */
static int espdet_overlay_enum_caps(struct mpipe_pad *pad, uint32_t index,
				    const struct mpipe_structure *filter,
				    struct mpipe_structure *out)
{
	static const uint32_t formats[] = {VIDEO_PIX_FMT_RGB565X, VIDEO_PIX_FMT_RGB565};
	struct mpipe_structure candidate;
	int ret;

	ARG_UNUSED(pad);

	if (index >= ARRAY_SIZE(formats)) {
		return -ENOENT;
	}

	ret = mpipe_structure_init_fields(&candidate, MPIPE_MEDIA_VIDEO, MPIPE_CAPS_PIXEL_FORMAT,
					  MPIPE_TYPE_UINT, formats[index], MPIPE_CAPS_IMAGE_WIDTH,
					  MPIPE_TYPE_UINT_RANGE, FRAME_SIZE_MIN, FRAME_SIZE_MAX, 1,
					  MPIPE_CAPS_IMAGE_HEIGHT, MPIPE_TYPE_UINT_RANGE,
					  FRAME_SIZE_MIN, FRAME_SIZE_MAX, 1, MPIPE_CAPS_END);
	if (ret != 0) {
		return ret;
	}

	return mpipe_pad_enum_filter(&candidate, filter, out);
}

static int espdet_overlay_set_caps(struct mpipe_transform *transform,
				   enum mpipe_pad_direction direction,
				   const struct mpipe_structure *caps)
{
	struct espdet_overlay *self = (struct espdet_overlay *)transform;
	const struct mpipe_value *fmt = mpipe_structure_get_value(caps, MPIPE_CAPS_PIXEL_FORMAT);
	const struct mpipe_value *width = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_WIDTH);
	const struct mpipe_value *height = mpipe_structure_get_value(caps, MPIPE_CAPS_IMAGE_HEIGHT);
	size_t frame_size;

	if (direction != MPIPE_PAD_SINK) {
		return mpipe_transform_set_caps(transform, direction, caps);
	}

	if (fmt == NULL || width == NULL || height == NULL) {
		return -EINVAL;
	}

	self->width = mpipe_value_get_uint(width);
	self->height = mpipe_value_get_uint(height);
	self->pitch = self->width * 2U;
	self->big_endian = mpipe_value_get_uint(fmt) == VIDEO_PIX_FMT_RGB565X;
	frame_size = self->pitch * self->height;

	/* The frame copy only ever grows, and not while the detector reads it */
	if (self->snapshot == NULL || frame_size > self->snapshot->size) {
		if (self->snapshot != NULL) {
			if (!atomic_cas(&self->busy, 0, 1)) {
				return -EBUSY;
			}
			(void)video_buffer_release(self->snapshot);
			self->snapshot = NULL;
			atomic_clear(&self->busy);
		}

		self->snapshot = video_buffer_aligned_alloc(
			frame_size, CONFIG_VIDEO_BUFFER_POOL_ALIGN, K_NO_WAIT);
		if (self->snapshot == NULL) {
			LOG_ERR("Cannot allocate a %zu-byte frame copy", frame_size);
			return -ENOMEM;
		}
	}

	LOG_INF("Detecting on %ux%u %s frames", self->width, self->height,
		self->big_endian ? "RGB565X" : "RGB565");

	return mpipe_transform_set_caps(transform, direction, caps);
}

/* Drawing in place changes nothing to the buffers: pass the pool negotiation through */
static int espdet_overlay_decide_buffer_pool(struct mpipe_transform *transform,
					     struct mpipe_dispatch *query)
{
	struct espdet_overlay *self = (struct espdet_overlay *)transform;

	self->down_pool = query->pool;
	self->down_pool_cfg = query->pool_cfg;

	return 0;
}

static int espdet_overlay_propose_buffer_pool(struct mpipe_transform *transform,
					      struct mpipe_dispatch *query)
{
	struct espdet_overlay *self = (struct espdet_overlay *)transform;

	query->pool = self->down_pool;
	query->pool_cfg = self->down_pool_cfg;

	return 0;
}

int espdet_overlay_init(struct espdet_overlay *self, uint8_t id)
{
	struct mpipe_transform *transform = &self->transform;
	int ret;

	ret = mpipe_transform_init(transform, id);
	if (ret != 0) {
		return ret;
	}

	mpipe_element_set_name(&transform->element, "espdet_overlay");

	transform->mode = MPIPE_MODE_INPLACE;
	transform->set_caps = espdet_overlay_set_caps;
	transform->decide_buffer_pool = espdet_overlay_decide_buffer_pool;
	transform->propose_buffer_pool = espdet_overlay_propose_buffer_pool;
	transform->sink_pad.chain_fn = espdet_overlay_chain;
	transform->sink_pad.enum_caps_fn = espdet_overlay_enum_caps;
	transform->src_pad.enum_caps_fn = espdet_overlay_enum_caps;

	k_sem_init(&self->frame_ready, 0, 1);
	k_mutex_init(&self->lock);
	atomic_set(&self->busy, 1);
	self->fps_start = k_uptime_get();

	parse_class_labels();

	k_thread_create(&espdet_thread, espdet_stack, K_THREAD_STACK_SIZEOF(espdet_stack),
			espdet_thread_fn, self, NULL, NULL, CONFIG_ESPDET_THREAD_PRIORITY, 0,
			K_NO_WAIT);
	k_thread_name_set(&espdet_thread, "espdet");

	return 0;
}
