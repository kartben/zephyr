/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT virtio_sound

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/drivers/virtio.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(i2s_virtio_sound, CONFIG_I2S_LOG_LEVEL);

/*
 * Definitions below follow the Virtual I/O Device (VIRTIO) Version 1.3
 * specification, section 5.14 ("Sound Device").
 */

/* Device feature bits, spec 5.14.3 */
#define VIRTIO_SND_F_CTLS 0

/* Virtqueue indices, spec 5.14.2 */
#define VSND_VQ_CONTROL 0
#define VSND_VQ_EVENT   1
#define VSND_VQ_TX      2
#define VSND_VQ_RX      3
#define VSND_VQ_COUNT   4

/* Request codes, spec 5.14.6 */
#define VIRTIO_SND_R_JACK_INFO      1
#define VIRTIO_SND_R_PCM_INFO       0x0100
#define VIRTIO_SND_R_PCM_SET_PARAMS 0x0101
#define VIRTIO_SND_R_PCM_PREPARE    0x0102
#define VIRTIO_SND_R_PCM_RELEASE    0x0103
#define VIRTIO_SND_R_PCM_START      0x0104
#define VIRTIO_SND_R_PCM_STOP       0x0105
#define VIRTIO_SND_R_CHMAP_INFO     0x0200

/* Event codes, spec 5.14.6.3 */
#define VIRTIO_SND_EVT_JACK_CONNECTED     0x1000
#define VIRTIO_SND_EVT_JACK_DISCONNECTED  0x1001
#define VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED 0x1100
#define VIRTIO_SND_EVT_PCM_XRUN           0x1101

/* Status codes, spec 5.14.6 */
#define VIRTIO_SND_S_OK       0x8000
#define VIRTIO_SND_S_BAD_MSG  0x8001
#define VIRTIO_SND_S_NOT_SUPP 0x8002
#define VIRTIO_SND_S_IO_ERR   0x8003

/* Data flow directions, spec 5.14.6.6 */
#define VIRTIO_SND_D_OUTPUT 0
#define VIRTIO_SND_D_INPUT  1

/* PCM stream features, spec 5.14.6.6.1 */
#define VIRTIO_SND_PCM_F_EVT_XRUNS 4

/*
 * PCM sample formats, spec 5.14.6.6.1. S24 holds 24 valid bits in a 32 bit
 * container, matching the I2S API; S24_3 (11) is the packed three byte
 * variant and is deliberately not used.
 */
#define VIRTIO_SND_PCM_FMT_S8  3
#define VIRTIO_SND_PCM_FMT_S16 5
#define VIRTIO_SND_PCM_FMT_S24 15
#define VIRTIO_SND_PCM_FMT_S32 17

struct virtio_snd_config {
	uint32_t jacks;
	uint32_t streams;
	uint32_t chmaps;
	uint32_t controls;
} __packed;

struct virtio_snd_hdr {
	uint32_t code;
} __packed;

struct virtio_snd_query_info {
	struct virtio_snd_hdr hdr;
	uint32_t start_id;
	uint32_t count;
	uint32_t size;
} __packed;

struct virtio_snd_info {
	uint32_t hda_fn_nid;
} __packed;

struct virtio_snd_pcm_info {
	struct virtio_snd_info hdr;
	uint32_t features;
	uint64_t formats;
	uint64_t rates;
	uint8_t direction;
	uint8_t channels_min;
	uint8_t channels_max;
	uint8_t padding[5];
} __packed;

struct virtio_snd_pcm_hdr {
	struct virtio_snd_hdr hdr;
	uint32_t stream_id;
} __packed;

struct virtio_snd_pcm_set_params {
	struct virtio_snd_pcm_hdr hdr;
	uint32_t buffer_bytes;
	uint32_t period_bytes;
	uint32_t features;
	uint8_t channels;
	uint8_t format;
	uint8_t rate;
	uint8_t padding;
} __packed;

struct virtio_snd_pcm_xfer {
	uint32_t stream_id;
} __packed;

struct virtio_snd_pcm_status {
	uint32_t status;
	uint32_t latency_bytes;
} __packed;

struct virtio_snd_event {
	struct virtio_snd_hdr hdr;
	uint32_t data;
} __packed;

BUILD_ASSERT(sizeof(struct virtio_snd_pcm_info) == 32);
BUILD_ASSERT(sizeof(struct virtio_snd_pcm_set_params) == 24);

/* Sampling rates, indexed by the VIRTIO_SND_PCM_RATE_* value, spec 5.14.6.6.1 */
static const uint32_t vsnd_rates[] = {
	5512, 8000, 11025, 16000, 22050, 32000, 44100,
	48000, 64000, 88200, 96000, 176400, 192000, 384000,
};

#define VSND_DIR_TX BIT(0)
#define VSND_DIR_RX BIT(1)

/* Descriptors per I/O chain: transfer header, PCM payload and status */
#define VSND_IO_CHAIN_DESCS 3

#define VSND_DRAIN_MAX_MS 1000U

#define VSND_MAX_BLOCKS MAX(CONFIG_I2S_VIRTIO_SOUND_TX_BLOCKS, CONFIG_I2S_VIRTIO_SOUND_RX_BLOCKS)

/* Mirrors the virtio PCM stream lifecycle described in spec 5.14.6.6 */
enum vsnd_hw_state {
	VSND_HW_RELEASED = 0,
	VSND_HW_PARAMS_SET,
	VSND_HW_STARTED,
};

struct vsnd_stream;

struct vsnd_req {
	void *fifo_reserved;
	struct virtio_snd_pcm_xfer xfer;
	struct virtio_snd_pcm_status status;
	struct vsnd_stream *stream;
	void *mem_block;
	size_t size;
	uint16_t idx;
};

struct vsnd_block {
	void *mem_block;
	size_t size;
};

struct vsnd_stream {
	const struct device *dev;
	struct i2s_config cfg;
	enum i2s_state state;
	enum vsnd_hw_state hw_state;
	bool present;
	bool starved_logged;
	bool draining;
	uint32_t latency_bytes;
	enum i2s_dir dir;
	uint32_t stream_id;
	uint16_t vq_idx;

	/* Capabilities cached from VIRTIO_SND_R_PCM_INFO */
	uint64_t formats;
	uint64_t rates;
	uint32_t hw_features;
	uint8_t channels_min;
	uint8_t channels_max;

	/* Negotiated parameters, only valid once hw_state != VSND_HW_RELEASED */
	uint8_t fmt;
	uint8_t rate_idx;

	struct k_spinlock lock;
	uint16_t in_flight;
	uint16_t n_reqs;
	struct k_sem idle_sem;
	struct k_stack free_reqs;
	stack_data_t free_reqs_buf[VSND_MAX_BLOCKS];
	struct k_work stop_work;
	struct vsnd_req reqs[VSND_MAX_BLOCKS];

	/* TX only: blocks written before I2S_TRIGGER_START */
	struct k_fifo pending_tx;

	/* RX only: blocks completed by the device and waiting for i2s_read() */
	struct k_msgq rxq;
	char rxq_buf[CONFIG_I2S_VIRTIO_SOUND_RX_QUEUE_SIZE * sizeof(struct vsnd_block)];
};

struct vsnd_config {
	const struct device *vdev;
	int32_t tx_stream_id;
	int32_t rx_stream_id;
	uint8_t dir_mask;
};

#ifdef CONFIG_I2S_VIRTIO_SOUND_EVENTQ
struct vsnd_event_ctx {
	const struct device *dev;
	uint16_t idx;
};
#endif

struct vsnd_data {
	const struct device *dev;
	uint32_t n_streams;
	bool dead;
	struct vsnd_stream tx;
	struct vsnd_stream rx;

	/* Control queue, one transaction at a time */
	struct k_mutex ctl_mutex;
	struct k_sem ctl_sem;
	union {
		struct virtio_snd_hdr hdr;
		struct virtio_snd_pcm_hdr pcm;
		struct virtio_snd_pcm_set_params params;
		struct virtio_snd_query_info query;
	} ctl_req;
	struct virtio_snd_hdr ctl_resp;
	struct virtio_snd_pcm_info pcm_info[CONFIG_I2S_VIRTIO_SOUND_MAX_STREAMS];

#ifdef CONFIG_I2S_VIRTIO_SOUND_EVENTQ
	struct virtio_snd_event events[CONFIG_I2S_VIRTIO_SOUND_EVENT_BUFFERS];
	struct vsnd_event_ctx event_ctx[CONFIG_I2S_VIRTIO_SOUND_EVENT_BUFFERS];
#endif

	struct k_work_q workq;

	K_KERNEL_STACK_MEMBER(workq_stack, CONFIG_I2S_VIRTIO_SOUND_WORKQ_STACK_SIZE);
};

static void vsnd_rx_cb(void *opaque, uint32_t used_len);

static uint32_t vsnd_word_size_bytes(uint8_t word_size)
{
	if (word_size <= 8U) {
		return 1U;
	}

	if (word_size <= 16U) {
		return 2U;
	}

	return 4U;
}

static uint32_t vsnd_frame_bytes(const struct i2s_config *cfg)
{
	uint8_t channels = cfg->channels;

	if ((cfg->format & I2S_FMT_DATA_FORMAT_MASK) == I2S_FMT_DATA_FORMAT_I2S) {
		channels = 2U;
	}

	if (channels == 0U) {
		return 0U;
	}

	return channels * vsnd_word_size_bytes(cfg->word_size);
}

static int vsnd_word_size_to_fmt(uint8_t word_size)
{
	switch (word_size) {
	case 8:
		return VIRTIO_SND_PCM_FMT_S8;
	case 16:
		return VIRTIO_SND_PCM_FMT_S16;
	case 24:
		return VIRTIO_SND_PCM_FMT_S24;
	case 32:
		return VIRTIO_SND_PCM_FMT_S32;
	default:
		return -EINVAL;
	}
}

static int vsnd_freq_to_rate_idx(uint32_t frame_clk_freq)
{
	for (size_t i = 0; i < ARRAY_SIZE(vsnd_rates); i++) {
		if (vsnd_rates[i] == frame_clk_freq) {
			return (int)i;
		}
	}

	return -EINVAL;
}

/*
 * virtq_create() requires a non-zero queue size to be a power of two, which the
 * virtio_enumerate_queues callback contract does not mention.
 */
static uint16_t vsnd_queue_size(uint16_t want, uint16_t max)
{
	uint16_t n = 1;

	if (max == 0U || want == 0U) {
		return 0U;
	}

	while (n < want && n < max) {
		n <<= 1;
	}

	while (n > max) {
		n >>= 1;
	}

	return n;
}

static struct vsnd_stream *vsnd_get_stream(const struct device *dev, enum i2s_dir dir)
{
	struct vsnd_data *data = dev->data;

	switch (dir) {
	case I2S_DIR_RX:
		return &data->rx;
	case I2S_DIR_TX:
		return &data->tx;
	default:
		return NULL;
	}
}

static void vsnd_ctl_cb(void *opaque, uint32_t used_len)
{
	struct vsnd_data *data = opaque;

	ARG_UNUSED(used_len);

	k_sem_give(&data->ctl_sem);
}

static int vsnd_status_to_errno(uint32_t status)
{
	switch (status) {
	case VIRTIO_SND_S_OK:
		return 0;
	case VIRTIO_SND_S_NOT_SUPP:
		return -ENOTSUP;
	case VIRTIO_SND_S_BAD_MSG:
		return -EINVAL;
	default:
		return -EIO;
	}
}

static int vsnd_ctl_xfer(const struct device *dev, size_t req_len, void *resp_payload,
			 size_t resp_len)
{
	const struct vsnd_config *cfg = dev->config;
	struct vsnd_data *data = dev->data;
	struct virtq *vq = virtio_get_virtqueue(cfg->vdev, VSND_VQ_CONTROL);
	struct virtq_buf bufs[3];
	uint16_t n_bufs = 2;
	int ret;

	__ASSERT(!k_is_in_isr(), "control queue transactions require thread context");
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	if (vq == NULL) {
		return -ENODEV;
	}

	if (data->dead) {
		return -EIO;
	}

	k_mutex_lock(&data->ctl_mutex, K_FOREVER);

	k_sem_reset(&data->ctl_sem);
	data->ctl_resp.code = 0;

	bufs[0].addr = &data->ctl_req;
	bufs[0].len = req_len;
	bufs[1].addr = &data->ctl_resp;
	bufs[1].len = sizeof(data->ctl_resp);

	if (resp_payload != NULL && resp_len > 0U) {
		bufs[2].addr = resp_payload;
		bufs[2].len = resp_len;
		n_bufs = 3;
	}

	/*
	 * K_NO_WAIT cannot fail here: the queue is sized for a full chain and
	 * the mutex above serializes transactions. See vsnd_post_tx().
	 */
	ret = virtq_add_buffer_chain(vq, bufs, n_bufs, 1, vsnd_ctl_cb, data, K_NO_WAIT);
	if (ret != 0) {
		LOG_ERR("failed to enqueue control request: %d", ret);
		k_mutex_unlock(&data->ctl_mutex);
		return -EIO;
	}

	virtio_notify_virtqueue(cfg->vdev, VSND_VQ_CONTROL);

	/*
	 * Never wait forever: this runs at POST_KERNEL and a device that never
	 * completes the request would otherwise hang the boot.
	 */
	ret = k_sem_take(&data->ctl_sem, K_MSEC(CONFIG_I2S_VIRTIO_SOUND_CTL_TIMEOUT_MS));
	if (ret != 0) {
		/*
		 * The descriptors are still owned by the device and the virtio
		 * core offers no way to reclaim them, so the device is left
		 * unusable rather than pretending to recover.
		 */
		LOG_ERR("control request timed out, device is now unusable");
		data->dead = true;
		k_mutex_unlock(&data->ctl_mutex);
		return -ETIMEDOUT;
	}

	ret = vsnd_status_to_errno(sys_le32_to_cpu(data->ctl_resp.code));

	k_mutex_unlock(&data->ctl_mutex);

	return ret;
}

static int vsnd_pcm_simple_cmd(struct vsnd_stream *stream, uint32_t code)
{
	const struct device *dev = stream->dev;
	struct vsnd_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->ctl_mutex, K_FOREVER);
	data->ctl_req.pcm.hdr.code = sys_cpu_to_le32(code);
	data->ctl_req.pcm.stream_id = sys_cpu_to_le32(stream->stream_id);
	k_mutex_unlock(&data->ctl_mutex);

	ret = vsnd_ctl_xfer(dev, sizeof(struct virtio_snd_pcm_hdr), NULL, 0);
	if (ret != 0) {
		LOG_ERR("PCM command 0x%04x on stream %u failed: %d", code, stream->stream_id, ret);
	}

	return ret;
}

static int vsnd_pcm_set_params(struct vsnd_stream *stream)
{
	const struct device *dev = stream->dev;
	struct vsnd_data *data = dev->data;
	const struct i2s_config *cfg = &stream->cfg;
	uint32_t features = 0;
	uint8_t channels = cfg->channels;
	uint32_t bytes_per_sec;
	int ret;

	if ((cfg->format & I2S_FMT_DATA_FORMAT_MASK) == I2S_FMT_DATA_FORMAT_I2S) {
		channels = 2U;
	}

	if (IS_ENABLED(CONFIG_I2S_VIRTIO_SOUND_EVENTQ) &&
	    (stream->hw_features & BIT(VIRTIO_SND_PCM_F_EVT_XRUNS)) != 0U) {
		features |= BIT(VIRTIO_SND_PCM_F_EVT_XRUNS);
	}

	k_mutex_lock(&data->ctl_mutex, K_FOREVER);
	data->ctl_req.params.hdr.hdr.code = sys_cpu_to_le32(VIRTIO_SND_R_PCM_SET_PARAMS);
	data->ctl_req.params.hdr.stream_id = sys_cpu_to_le32(stream->stream_id);
	data->ctl_req.params.buffer_bytes = sys_cpu_to_le32(cfg->block_size * stream->n_reqs);
	data->ctl_req.params.period_bytes = sys_cpu_to_le32(cfg->block_size);
	data->ctl_req.params.features = sys_cpu_to_le32(features);
	data->ctl_req.params.channels = channels;
	data->ctl_req.params.format = stream->fmt;
	data->ctl_req.params.rate = stream->rate_idx;
	data->ctl_req.params.padding = 0;
	k_mutex_unlock(&data->ctl_mutex);

	ret = vsnd_ctl_xfer(dev, sizeof(struct virtio_snd_pcm_set_params), NULL, 0);
	if (ret != 0) {
		LOG_ERR("PCM_SET_PARAMS on stream %u failed: %d", stream->stream_id, ret);
		return ret;
	}

	stream->hw_state = VSND_HW_PARAMS_SET;

	/* Buffer depth in ms; short buffers turn scheduling delays into gaps */
	bytes_per_sec = (uint32_t)cfg->frame_clk_freq * vsnd_frame_bytes(cfg);
	if (bytes_per_sec != 0U) {
		LOG_DBG("stream %u: %u Hz, %u ch, %u bit, period %zu B, buffer %zu B (%u ms)",
			stream->stream_id, cfg->frame_clk_freq, channels, cfg->word_size,
			cfg->block_size, cfg->block_size * stream->n_reqs,
			(uint32_t)((cfg->block_size * stream->n_reqs * 1000U) / bytes_per_sec));
	}

	return 0;
}

/*
 * Time the device still needs to play out what it has already accepted. A
 * transfer completion only means the block was taken, so releasing the stream
 * right after the last one would flush audio that was never heard: the spec
 * has PCM_RELEASE complete all pending I/O messages, and QEMU implements that
 * by flushing. The device reports what it is still holding in the latency
 * field of the transfer status; when it does not, fall back to assuming the
 * buffer we asked for is full.
 */
static uint32_t vsnd_drain_time_ms(struct vsnd_stream *stream)
{
	uint32_t bytes_per_sec =
		(uint32_t)stream->cfg.frame_clk_freq * vsnd_frame_bytes(&stream->cfg);
	uint32_t bytes = stream->latency_bytes;

	if (bytes_per_sec == 0U) {
		return 0U;
	}

	if (bytes == 0U) {
		bytes = (uint32_t)stream->cfg.block_size * stream->n_reqs;
	}

	return MIN((bytes * 1000U) / bytes_per_sec + 1U, VSND_DRAIN_MAX_MS);
}

static void vsnd_stop_work(struct k_work *work)
{
	struct vsnd_stream *stream = CONTAINER_OF(work, struct vsnd_stream, stop_work);

	if (stream->draining) {
		uint32_t wait_ms = vsnd_drain_time_ms(stream);

		stream->draining = false;
		LOG_DBG("draining %s stream, waiting %u ms for playout",
			stream->dir == I2S_DIR_TX ? "TX" : "RX", wait_ms);
		k_msleep(wait_ms);
	}

	if (stream->hw_state == VSND_HW_STARTED) {
		(void)vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_STOP);
	}

	if (stream->hw_state != VSND_HW_RELEASED) {
		(void)vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_RELEASE);
		stream->hw_state = VSND_HW_RELEASED;
	}

	/* An error state is only left through I2S_TRIGGER_PREPARE */
	K_SPINLOCK(&stream->lock) {
		if (stream->state == I2S_STATE_STOPPING) {
			stream->state = I2S_STATE_READY;
		}
	}
}

/* Must be called with stream->lock held */
static void vsnd_enter_error(struct vsnd_stream *stream)
{
	struct vsnd_data *data = stream->dev->data;

	if (stream->state == I2S_STATE_ERROR) {
		return;
	}

	LOG_ERR("%s stream entered error state", stream->dir == I2S_DIR_TX ? "TX" : "RX");
	stream->state = I2S_STATE_ERROR;
	k_work_submit_to_queue(&data->workq, &stream->stop_work);
}

static void vsnd_tx_cb(void *opaque, uint32_t used_len)
{
	struct vsnd_req *req = opaque;
	struct vsnd_stream *tx = req->stream;
	struct vsnd_data *data = tx->dev->data;
	uint32_t status = sys_le32_to_cpu(req->status.status);

	ARG_UNUSED(used_len);

	tx->latency_bytes = sys_le32_to_cpu(req->status.latency_bytes);

	k_mem_slab_free(tx->cfg.mem_slab, req->mem_block);
	req->mem_block = NULL;

	K_SPINLOCK(&tx->lock) {
		tx->in_flight--;

		if (status != VIRTIO_SND_S_OK) {
			LOG_ERR("TX transfer failed with status 0x%04x", status);
			vsnd_enter_error(tx);
		} else if (tx->in_flight == 0U && tx->state == I2S_STATE_STOPPING) {
			k_work_submit_to_queue(&data->workq, &tx->stop_work);
		}

		/*
		 * Running out of queued blocks is deliberately not treated as
		 * an underrun. A completion only means the device took the
		 * block, not that it played it, so an application that is
		 * simply slower than the ISR would be pushed into the error
		 * state while it is still feeding the stream normally. Real
		 * underruns are reported by the device, either through the
		 * transfer status above or through a PCM_XRUN event.
		 */
		if (tx->in_flight == 0U && tx->state == I2S_STATE_RUNNING &&
		    !tx->starved_logged) {
			tx->starved_logged = true;
			LOG_WRN("TX ran out of queued blocks; expect gaps in the output. "
				"Queue more blocks ahead of time, use a larger block_size, "
				"or raise CONFIG_I2S_VIRTIO_SOUND_TX_BLOCKS.");
		}

		if (tx->in_flight == 0U) {
			k_sem_give(&tx->idle_sem);
		}
	}

	k_stack_push(&tx->free_reqs, (stack_data_t)req->idx);
}

/* Must be called with rx->lock held. Does not notify the device. */
static int vsnd_post_rx(struct vsnd_stream *rx, struct vsnd_req *req)
{
	const struct vsnd_config *cfg = rx->dev->config;
	struct virtq *vq = virtio_get_virtqueue(cfg->vdev, VSND_VQ_RX);
	struct virtq_buf bufs[VSND_IO_CHAIN_DESCS];
	void *block;
	int ret;

	if (k_mem_slab_alloc(rx->cfg.mem_slab, &block, K_NO_WAIT) != 0) {
		return -ENOMEM;
	}

	req->mem_block = block;
	req->xfer.stream_id = sys_cpu_to_le32(rx->stream_id);
	memset(&req->status, 0, sizeof(req->status));

	bufs[0].addr = &req->xfer;
	bufs[0].len = sizeof(req->xfer);
	bufs[1].addr = block;
	bufs[1].len = rx->cfg.block_size;
	bufs[2].addr = &req->status;
	bufs[2].len = sizeof(req->status);

	/* Only the transfer header is device-readable on the RX queue */
	ret = virtq_add_buffer_chain(vq, bufs, VSND_IO_CHAIN_DESCS, 1, vsnd_rx_cb, req, K_NO_WAIT);
	if (ret != 0) {
		k_mem_slab_free(rx->cfg.mem_slab, block);
		req->mem_block = NULL;
		return -EIO;
	}

	rx->in_flight++;

	return 0;
}

static void vsnd_rx_cb(void *opaque, uint32_t used_len)
{
	struct vsnd_req *req = opaque;
	struct vsnd_stream *rx = req->stream;
	const struct vsnd_config *cfg = rx->dev->config;
	struct vsnd_data *data = rx->dev->data;
	uint32_t status = sys_le32_to_cpu(req->status.status);
	struct vsnd_block blk;
	bool notify = false;
	size_t len;

	/*
	 * The device writes the PCM payload followed by the status structure,
	 * so the payload length is whatever precedes the status.
	 */
	len = (used_len > sizeof(struct virtio_snd_pcm_status))
		      ? (used_len - sizeof(struct virtio_snd_pcm_status))
		      : 0U;
	len = MIN(len, rx->cfg.block_size);

	blk.mem_block = req->mem_block;
	blk.size = len;
	req->mem_block = NULL;

	K_SPINLOCK(&rx->lock) {
		rx->in_flight--;

		if (status != VIRTIO_SND_S_OK) {
			LOG_ERR("RX transfer failed with status 0x%04x", status);
			k_mem_slab_free(rx->cfg.mem_slab, blk.mem_block);
			vsnd_enter_error(rx);
		} else if (len == 0U) {
			k_mem_slab_free(rx->cfg.mem_slab, blk.mem_block);
		} else if (k_msgq_put(&rx->rxq, &blk, K_NO_WAIT) != 0) {
			/* The application is not reading fast enough */
			k_mem_slab_free(rx->cfg.mem_slab, blk.mem_block);
			vsnd_enter_error(rx);
		}

		if (rx->state == I2S_STATE_RUNNING) {
			if (vsnd_post_rx(rx, req) == 0) {
				notify = true;
			} else {
				/* No free block: overrun */
				vsnd_enter_error(rx);
				k_stack_push(&rx->free_reqs, (stack_data_t)req->idx);
			}
		} else {
			k_stack_push(&rx->free_reqs, (stack_data_t)req->idx);
		}

		if (rx->in_flight == 0U) {
			if (rx->state == I2S_STATE_STOPPING) {
				k_work_submit_to_queue(&data->workq, &rx->stop_work);
			}
			k_sem_give(&rx->idle_sem);
		}
	}

	if (notify) {
		virtio_notify_virtqueue(cfg->vdev, VSND_VQ_RX);
	}
}

/* Must be called with tx->lock held. Does not notify the device. */
static int vsnd_post_tx(struct vsnd_stream *tx, struct vsnd_req *req)
{
	const struct vsnd_config *cfg = tx->dev->config;
	struct virtq *vq = virtio_get_virtqueue(cfg->vdev, VSND_VQ_TX);
	struct virtq_buf bufs[VSND_IO_CHAIN_DESCS];
	int ret;

	bufs[0].addr = &req->xfer;
	bufs[0].len = sizeof(req->xfer);
	bufs[1].addr = req->mem_block;
	bufs[1].len = req->size;
	bufs[2].addr = &req->status;
	bufs[2].len = sizeof(req->status);

	/*
	 * K_NO_WAIT is used everywhere in this driver on purpose. Passing
	 * K_FOREVER to virtq_add_buffer_chain() makes it skip its free
	 * descriptor check and block on a k_stack while holding a spinlock.
	 * The request pool is capped at virtq::num / VSND_IO_CHAIN_DESCS so
	 * there are always enough free descriptors for a full chain anyway.
	 */
	ret = virtq_add_buffer_chain(vq, bufs, VSND_IO_CHAIN_DESCS, 2, vsnd_tx_cb, req, K_NO_WAIT);
	if (ret != 0) {
		return -EIO;
	}

	tx->in_flight++;

	return 0;
}

#ifdef CONFIG_I2S_VIRTIO_SOUND_EVENTQ
static void vsnd_event_cb(void *opaque, uint32_t used_len);

static int vsnd_post_event(const struct device *dev, uint16_t idx)
{
	const struct vsnd_config *cfg = dev->config;
	struct vsnd_data *data = dev->data;
	struct virtq *vq = virtio_get_virtqueue(cfg->vdev, VSND_VQ_EVENT);
	struct virtq_buf buf = {
		.addr = &data->events[idx],
		.len = sizeof(struct virtio_snd_event),
	};
	int ret;

	if (vq == NULL || vq->num == 0U) {
		return -ENODEV;
	}

	ret = virtq_add_buffer_chain(vq, &buf, 1, 0, vsnd_event_cb, &data->event_ctx[idx],
				     K_NO_WAIT);
	if (ret != 0) {
		return ret;
	}

	virtio_notify_virtqueue(cfg->vdev, VSND_VQ_EVENT);

	return 0;
}

static void vsnd_event_cb(void *opaque, uint32_t used_len)
{
	struct vsnd_event_ctx *ctx = opaque;
	const struct device *dev = ctx->dev;
	struct vsnd_data *data = dev->data;
	uint32_t code = sys_le32_to_cpu(data->events[ctx->idx].hdr.code);
	uint32_t payload = sys_le32_to_cpu(data->events[ctx->idx].data);

	ARG_UNUSED(used_len);

	switch (code) {
	case VIRTIO_SND_EVT_PCM_XRUN:
		if (data->tx.present && payload == data->tx.stream_id) {
			K_SPINLOCK(&data->tx.lock) {
				vsnd_enter_error(&data->tx);
			}
		}
		if (data->rx.present && payload == data->rx.stream_id) {
			K_SPINLOCK(&data->rx.lock) {
				vsnd_enter_error(&data->rx);
			}
		}
		break;
	case VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED:
		/* Transfer completions already tell us this */
		LOG_DBG("period elapsed on stream %u", payload);
		break;
	case VIRTIO_SND_EVT_JACK_CONNECTED:
	case VIRTIO_SND_EVT_JACK_DISCONNECTED:
		LOG_INF("jack %u %s", payload,
			code == VIRTIO_SND_EVT_JACK_CONNECTED ? "connected" : "disconnected");
		break;
	default:
		LOG_DBG("unhandled event 0x%04x (%u)", code, payload);
		break;
	}

	(void)vsnd_post_event(dev, ctx->idx);
}
#endif /* CONFIG_I2S_VIRTIO_SOUND_EVENTQ */

/* Shared by DROP, PREPARE and re-configuration */
static void vsnd_purge(struct vsnd_stream *stream)
{
	struct vsnd_req *req;
	struct vsnd_block blk;
	k_spinlock_key_t key;
	bool wait;

	/* Dropping discards the tail, so never wait for it to play out */
	stream->draining = false;

	if (stream->hw_state == VSND_HW_STARTED) {
		(void)vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_STOP);
	}

	if (stream->hw_state != VSND_HW_RELEASED) {
		k_sem_reset(&stream->idle_sem);

		key = k_spin_lock(&stream->lock);
		wait = (stream->in_flight != 0U);
		k_spin_unlock(&stream->lock, key);

		/*
		 * PCM_RELEASE is specified to complete all pending I/O
		 * messages, which is the only way to get the buffers back.
		 */
		(void)vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_RELEASE);
		stream->hw_state = VSND_HW_RELEASED;

		if (wait && k_sem_take(&stream->idle_sem,
				       K_MSEC(CONFIG_I2S_VIRTIO_SOUND_CTL_TIMEOUT_MS)) != 0) {
			LOG_ERR("device did not return all buffers on release");
		}
	}

	while ((req = k_fifo_get(&stream->pending_tx, K_NO_WAIT)) != NULL) {
		if (req->mem_block != NULL) {
			k_mem_slab_free(stream->cfg.mem_slab, req->mem_block);
			req->mem_block = NULL;
		}
		k_stack_push(&stream->free_reqs, (stack_data_t)req->idx);
	}

	while (k_msgq_get(&stream->rxq, &blk, K_NO_WAIT) == 0) {
		k_mem_slab_free(stream->cfg.mem_slab, blk.mem_block);
	}
}

static int vsnd_validate_cfg(struct vsnd_stream *stream, const struct i2s_config *cfg,
			     uint8_t *fmt_out, uint8_t *rate_out)
{
	uint32_t frame_bytes;
	uint8_t channels = cfg->channels;
	int fmt;
	int rate_idx;

	if (cfg->mem_slab == NULL || cfg->block_size == 0U) {
		LOG_ERR("mem_slab and block_size are mandatory");
		return -EINVAL;
	}

	if ((cfg->format & I2S_FMT_DATA_ORDER_LSB) != 0U) {
		LOG_ERR("only MSB first data order is supported");
		return -EINVAL;
	}

	switch (cfg->format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
		/* The API defines the classic I2S format as stereo only */
		channels = 2U;
		if (cfg->channels != 2U) {
			LOG_ERR("I2S data format requires 2 channels");
			return -EINVAL;
		}
		break;
	case I2S_FMT_DATA_FORMAT_PCM_SHORT:
	case I2S_FMT_DATA_FORMAT_PCM_LONG:
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		/*
		 * There is no wire format to speak of, the payload is always
		 * interleaved little endian PCM. Accept and ignore.
		 */
		break;
	default:
		LOG_ERR("unsupported data format");
		return -EINVAL;
	}

	if ((cfg->options & I2S_OPT_LOOPBACK) != 0U) {
		LOG_ERR("loopback is not supported");
		return -EINVAL;
	}

	if ((cfg->options & I2S_OPT_PINGPONG) != 0U) {
		LOG_ERR("ping-pong mode is not supported");
		return -EINVAL;
	}

	if (channels < stream->channels_min || channels > stream->channels_max) {
		LOG_ERR("%u channels outside the device range %u..%u", channels,
			stream->channels_min, stream->channels_max);
		return -EINVAL;
	}

	fmt = vsnd_word_size_to_fmt(cfg->word_size);
	if (fmt < 0) {
		LOG_ERR("unsupported word size %u", cfg->word_size);
		return -EINVAL;
	}

	if ((stream->formats & BIT64(fmt)) == 0ULL) {
		LOG_ERR("device does not support a %u bit sample format", cfg->word_size);
		return -EINVAL;
	}

	rate_idx = vsnd_freq_to_rate_idx(cfg->frame_clk_freq);
	if (rate_idx < 0) {
		LOG_ERR("unsupported sampling rate %u", cfg->frame_clk_freq);
		return -EINVAL;
	}

	if ((stream->rates & BIT64(rate_idx)) == 0ULL) {
		LOG_ERR("device does not support %u Hz", cfg->frame_clk_freq);
		return -EINVAL;
	}

	frame_bytes = vsnd_frame_bytes(cfg);
	if (frame_bytes == 0U || (cfg->block_size % frame_bytes) != 0U) {
		LOG_ERR("block_size %zu is not a multiple of the frame size %u", cfg->block_size,
			frame_bytes);
		return -EINVAL;
	}

	*fmt_out = (uint8_t)fmt;
	*rate_out = (uint8_t)rate_idx;

	return 0;
}

static int vsnd_configure(const struct device *dev, enum i2s_dir dir, const struct i2s_config *cfg)
{
	struct vsnd_stream *stream;
	uint8_t fmt;
	uint8_t rate_idx;
	uint32_t needed;
	int ret;

	if (dir == I2S_DIR_BOTH) {
		/* Each virtio stream is configured independently */
		return -ENOSYS;
	}

	stream = vsnd_get_stream(dev, dir);
	if (stream == NULL || !stream->present) {
		return -EINVAL;
	}

	__ASSERT(!k_is_in_isr(), "i2s_configure() requires thread context");
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	if (stream->state == I2S_STATE_RUNNING) {
		LOG_ERR("cannot reconfigure a running stream");
		return -EIO;
	}

	if (cfg->frame_clk_freq == 0U) {
		/* Zero frame clock means "release the stream" */
		vsnd_purge(stream);
		memset(&stream->cfg, 0, sizeof(stream->cfg));
		stream->state = I2S_STATE_NOT_READY;
		return 0;
	}

	ret = vsnd_validate_cfg(stream, cfg, &fmt, &rate_idx);
	if (ret != 0) {
		return ret;
	}

	if (dir == I2S_DIR_RX) {
		/* Two blocks posted to the device, two for the application */
		needed = 4U;
		if (cfg->mem_slab->info.num_blocks < needed) {
			LOG_WRN("RX mem_slab has %u blocks, at least %u recommended",
				cfg->mem_slab->info.num_blocks, needed);
		}
	}

	/*
	 * PCM_SET_PARAMS is only valid from the released or parameters-set
	 * state, so tear any previous setup down first.
	 */
	vsnd_purge(stream);

	stream->cfg = *cfg;
	stream->fmt = fmt;
	stream->rate_idx = rate_idx;

	ret = vsnd_pcm_set_params(stream);
	if (ret != 0) {
		memset(&stream->cfg, 0, sizeof(stream->cfg));
		stream->state = I2S_STATE_NOT_READY;
		return (ret == -ENOTSUP) ? -EINVAL : ret;
	}

	stream->state = I2S_STATE_READY;

	return 0;
}

static const struct i2s_config *vsnd_config_get(const struct device *dev, enum i2s_dir dir)
{
	struct vsnd_stream *stream = vsnd_get_stream(dev, dir);

	if (stream == NULL || !stream->present || stream->state == I2S_STATE_NOT_READY) {
		return NULL;
	}

	return &stream->cfg;
}

static int vsnd_write(const struct device *dev, void *mem_block, size_t size)
{
	const struct vsnd_config *cfg = dev->config;
	struct vsnd_data *data = dev->data;
	struct vsnd_stream *tx = &data->tx;
	struct vsnd_req *req;
	k_timeout_t timeout;
	stack_data_t idx;
	k_spinlock_key_t key;
	uint32_t frame_bytes;
	bool notify = false;
	int ret = 0;

	if (!tx->present) {
		return -EINVAL;
	}

	if (tx->state != I2S_STATE_READY && tx->state != I2S_STATE_RUNNING) {
		return -EIO;
	}

	frame_bytes = vsnd_frame_bytes(&tx->cfg);
	if (size == 0U || size > tx->cfg.block_size || frame_bytes == 0U ||
	    (size % frame_bytes) != 0U) {
		return -EINVAL;
	}

	timeout = k_is_in_isr() ? K_NO_WAIT : SYS_TIMEOUT_MS(tx->cfg.timeout);

	if (k_stack_pop(&tx->free_reqs, &idx, timeout) != 0) {
		return K_TIMEOUT_EQ(timeout, K_NO_WAIT) ? -EBUSY : -EAGAIN;
	}

	req = &tx->reqs[idx];
	req->mem_block = mem_block;
	req->size = size;
	req->xfer.stream_id = sys_cpu_to_le32(tx->stream_id);
	memset(&req->status, 0, sizeof(req->status));

	key = k_spin_lock(&tx->lock);

	if (tx->state == I2S_STATE_RUNNING) {
		ret = vsnd_post_tx(tx, req);
		notify = (ret == 0);
	} else {
		k_fifo_put(&tx->pending_tx, req);
	}

	k_spin_unlock(&tx->lock, key);

	if (ret != 0) {
		/* The caller keeps ownership of mem_block on failure */
		req->mem_block = NULL;
		k_stack_push(&tx->free_reqs, idx);
		return ret;
	}

	if (notify) {
		virtio_notify_virtqueue(cfg->vdev, VSND_VQ_TX);
	}

	return 0;
}

static int vsnd_read(const struct device *dev, void **mem_block, size_t *size)
{
	struct vsnd_data *data = dev->data;
	struct vsnd_stream *rx = &data->rx;
	struct vsnd_block blk;
	k_timeout_t timeout;

	if (!rx->present || rx->state == I2S_STATE_NOT_READY) {
		return -EIO;
	}

	if (rx->state == I2S_STATE_ERROR) {
		/* Data received before the error is still valid, drain it */
		timeout = K_NO_WAIT;
	} else {
		timeout = k_is_in_isr() ? K_NO_WAIT : SYS_TIMEOUT_MS(rx->cfg.timeout);
	}

	if (k_msgq_get(&rx->rxq, &blk, timeout) != 0) {
		if (rx->state == I2S_STATE_ERROR) {
			return -EIO;
		}
		return K_TIMEOUT_EQ(timeout, K_NO_WAIT) ? -EBUSY : -EAGAIN;
	}

	*mem_block = blk.mem_block;
	*size = blk.size;

	return 0;
}

static int vsnd_trigger_start(struct vsnd_stream *stream)
{
	const struct vsnd_config *cfg = stream->dev->config;
	struct vsnd_req *req;
	k_spinlock_key_t key;
	bool posted = false;
	int ret;

	if (stream->state != I2S_STATE_READY) {
		LOG_ERR("START is only valid from the ready state");
		return -EIO;
	}

	if (stream->dir == I2S_DIR_TX && k_fifo_is_empty(&stream->pending_tx)) {
		LOG_ERR("no data queued for transmission");
		return -EIO;
	}

	if (stream->hw_state == VSND_HW_RELEASED) {
		ret = vsnd_pcm_set_params(stream);
		if (ret != 0) {
			return -EIO;
		}
	}

	ret = vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_PREPARE);
	if (ret != 0) {
		return -EIO;
	}

	key = k_spin_lock(&stream->lock);
	stream->state = I2S_STATE_RUNNING;
	stream->starved_logged = false;

	if (stream->dir == I2S_DIR_TX) {
		while ((req = k_fifo_get(&stream->pending_tx, K_NO_WAIT)) != NULL) {
			if (vsnd_post_tx(stream, req) != 0) {
				k_mem_slab_free(stream->cfg.mem_slab, req->mem_block);
				req->mem_block = NULL;
				k_stack_push(&stream->free_reqs, (stack_data_t)req->idx);
				continue;
			}
			posted = true;
		}
	} else {
		/*
		 * Never claim more than half of the memory slab, so that an
		 * application sharing one slab between capture and playback
		 * still has blocks left to work with.
		 */
		uint32_t half = stream->cfg.mem_slab->info.num_blocks / 2U;
		uint16_t n_post = MIN(stream->n_reqs, MAX(half, 1U));

		for (uint16_t i = 0; i < n_post; i++) {
			stack_data_t idx;

			if (k_stack_pop(&stream->free_reqs, &idx, K_NO_WAIT) != 0) {
				break;
			}

			if (vsnd_post_rx(stream, &stream->reqs[idx]) != 0) {
				k_stack_push(&stream->free_reqs, idx);
				break;
			}
			posted = true;
		}
	}

	if (!posted) {
		stream->state = I2S_STATE_READY;
	}
	k_spin_unlock(&stream->lock, key);

	if (!posted) {
		LOG_ERR("failed to queue any buffer");
		(void)vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_RELEASE);
		stream->hw_state = VSND_HW_RELEASED;
		return -EIO;
	}

	virtio_notify_virtqueue(cfg->vdev, stream->vq_idx);

	ret = vsnd_pcm_simple_cmd(stream, VIRTIO_SND_R_PCM_START);
	if (ret != 0) {
		key = k_spin_lock(&stream->lock);
		stream->state = I2S_STATE_READY;
		k_spin_unlock(&stream->lock, key);
		vsnd_purge(stream);
		return -EIO;
	}

	stream->hw_state = VSND_HW_STARTED;

	return 0;
}

static int vsnd_trigger_stop(struct vsnd_stream *stream, bool drain)
{
	struct vsnd_data *data = stream->dev->data;
	k_spinlock_key_t key;
	bool idle;

	if (stream->state != I2S_STATE_RUNNING) {
		LOG_ERR("STOP is only valid from the running state");
		return -EIO;
	}

	key = k_spin_lock(&stream->lock);
	stream->state = I2S_STATE_STOPPING;
	stream->draining = drain;
	idle = (stream->in_flight == 0U);
	k_spin_unlock(&stream->lock, key);

	/*
	 * Otherwise the completion of the last outstanding block submits it.
	 * Either way stop_work is what takes the stream back to ready.
	 */
	if (idle) {
		k_work_submit_to_queue(&data->workq, &stream->stop_work);
	}

	return 0;
}

static int vsnd_trigger_single(struct vsnd_stream *stream, enum i2s_trigger_cmd cmd)
{
	k_spinlock_key_t key;
	int ret;

	switch (cmd) {
	case I2S_TRIGGER_START:
		return vsnd_trigger_start(stream);

	case I2S_TRIGGER_STOP:
		return vsnd_trigger_stop(stream, false);

	case I2S_TRIGGER_DRAIN:
		/*
		 * Only transmission has anything to play out; on capture the
		 * API defines DRAIN as equivalent to STOP.
		 */
		return vsnd_trigger_stop(stream, stream->dir == I2S_DIR_TX);

	case I2S_TRIGGER_DROP:
		if (stream->state == I2S_STATE_NOT_READY) {
			return -EIO;
		}
		key = k_spin_lock(&stream->lock);
		stream->state = I2S_STATE_STOPPING;
		k_spin_unlock(&stream->lock, key);
		vsnd_purge(stream);
		stream->state = I2S_STATE_READY;
		return 0;

	case I2S_TRIGGER_PREPARE:
		if (stream->state != I2S_STATE_ERROR) {
			return -EIO;
		}
		vsnd_purge(stream);
		ret = vsnd_pcm_set_params(stream);
		if (ret != 0) {
			return -EIO;
		}
		stream->state = I2S_STATE_READY;
		return 0;

	default:
		return -EINVAL;
	}
}

static int vsnd_trigger(const struct device *dev, enum i2s_dir dir, enum i2s_trigger_cmd cmd)
{
	struct vsnd_data *data = dev->data;
	struct vsnd_stream *stream;
	int ret;

	__ASSERT(!k_is_in_isr(), "i2s_trigger() requires thread context");
	if (k_is_in_isr()) {
		return -EWOULDBLOCK;
	}

	if (dir == I2S_DIR_BOTH) {
		if (!data->rx.present || !data->tx.present) {
			return -EINVAL;
		}

		ret = vsnd_trigger_single(&data->rx, cmd);
		if (ret != 0) {
			return ret;
		}

		return vsnd_trigger_single(&data->tx, cmd);
	}

	stream = vsnd_get_stream(dev, dir);
	if (stream == NULL || !stream->present) {
		return -EINVAL;
	}

	return vsnd_trigger_single(stream, cmd);
}

static DEVICE_API(i2s, vsnd_api) = {
	.configure = vsnd_configure,
	.config_get = vsnd_config_get,
	.read = vsnd_read,
	.write = vsnd_write,
	.trigger = vsnd_trigger,
};

static uint16_t vsnd_enum_queues_cb(uint16_t queue_idx, uint16_t max_queue_size, void *opaque)
{
	const struct device *dev = opaque;
	const struct vsnd_config *cfg = dev->config;

	switch (queue_idx) {
	case VSND_VQ_CONTROL:
		return vsnd_queue_size(4, max_queue_size);
	case VSND_VQ_EVENT:
#ifdef CONFIG_I2S_VIRTIO_SOUND_EVENTQ
		return vsnd_queue_size(CONFIG_I2S_VIRTIO_SOUND_EVENT_BUFFERS, max_queue_size);
#else
		return 0;
#endif
	case VSND_VQ_TX:
		if ((cfg->dir_mask & VSND_DIR_TX) == 0U) {
			return 0;
		}
		return vsnd_queue_size(VSND_IO_CHAIN_DESCS * CONFIG_I2S_VIRTIO_SOUND_TX_BLOCKS,
					 max_queue_size);
	case VSND_VQ_RX:
		if ((cfg->dir_mask & VSND_DIR_RX) == 0U) {
			return 0;
		}
		return vsnd_queue_size(VSND_IO_CHAIN_DESCS * CONFIG_I2S_VIRTIO_SOUND_RX_BLOCKS,
					 max_queue_size);
	default:
		return 0;
	}
}

static void vsnd_stream_init(struct vsnd_stream *stream, const struct device *dev, enum i2s_dir dir)
{
	stream->dev = dev;
	stream->dir = dir;
	stream->state = I2S_STATE_NOT_READY;
	stream->hw_state = VSND_HW_RELEASED;
	stream->vq_idx = (dir == I2S_DIR_TX) ? VSND_VQ_TX : VSND_VQ_RX;

	k_sem_init(&stream->idle_sem, 0, 1);
	k_stack_init(&stream->free_reqs, stream->free_reqs_buf, ARRAY_SIZE(stream->free_reqs_buf));
	k_work_init(&stream->stop_work, vsnd_stop_work);
	k_fifo_init(&stream->pending_tx);
	k_msgq_init(&stream->rxq, stream->rxq_buf, sizeof(struct vsnd_block),
		    CONFIG_I2S_VIRTIO_SOUND_RX_QUEUE_SIZE);

	for (uint16_t i = 0; i < ARRAY_SIZE(stream->reqs); i++) {
		stream->reqs[i].stream = stream;
		stream->reqs[i].idx = i;
	}
}

static int vsnd_stream_setup(struct vsnd_stream *stream, uint16_t want_blocks)
{
	const struct vsnd_config *cfg = stream->dev->config;
	struct virtq *vq = virtio_get_virtqueue(cfg->vdev, stream->vq_idx);

	if (vq == NULL || vq->num == 0U) {
		LOG_WRN("no virtqueue for the %s stream", stream->dir == I2S_DIR_TX ? "TX" : "RX");
		return -ENODEV;
	}

	/*
	 * Capping the pool at one full descriptor chain per request is what
	 * makes the K_NO_WAIT enqueues in this driver unable to fail.
	 */
	stream->n_reqs = MIN(want_blocks, vq->num / VSND_IO_CHAIN_DESCS);
	if (stream->n_reqs < 2U) {
		LOG_ERR("virtqueue %u is too small (%u descriptors)", stream->vq_idx, vq->num);
		return -ENOMEM;
	}

	if (stream->n_reqs < want_blocks) {
		LOG_WRN("virtqueue %u only fits %u of %u blocks", stream->vq_idx, stream->n_reqs,
			want_blocks);
	}

	for (uint16_t i = 0; i < stream->n_reqs; i++) {
		k_stack_push(&stream->free_reqs, (stack_data_t)i);
	}

	return 0;
}

static int vsnd_query_pcm_info(const struct device *dev, uint32_t count)
{
	struct vsnd_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->ctl_mutex, K_FOREVER);
	data->ctl_req.query.hdr.code = sys_cpu_to_le32(VIRTIO_SND_R_PCM_INFO);
	data->ctl_req.query.start_id = sys_cpu_to_le32(0);
	data->ctl_req.query.count = sys_cpu_to_le32(count);
	data->ctl_req.query.size = sys_cpu_to_le32(sizeof(struct virtio_snd_pcm_info));
	k_mutex_unlock(&data->ctl_mutex);

	ret = vsnd_ctl_xfer(dev, sizeof(struct virtio_snd_query_info), data->pcm_info,
			    count * sizeof(struct virtio_snd_pcm_info));
	if (ret != 0) {
		LOG_ERR("PCM_INFO query failed: %d", ret);
	}

	return ret;
}

static int vsnd_select_stream(const struct device *dev, struct vsnd_stream *stream, uint32_t count,
			      int32_t forced_id, uint8_t direction)
{
	struct vsnd_data *data = dev->data;
	const struct virtio_snd_pcm_info *info;
	uint32_t id = UINT32_MAX;

	if (forced_id >= 0) {
		if ((uint32_t)forced_id >= count) {
			LOG_ERR("stream %d requested in devicetree does not exist", forced_id);
			return -EINVAL;
		}
		if (data->pcm_info[forced_id].direction != direction) {
			LOG_ERR("stream %d is not a%s stream", forced_id,
				direction == VIRTIO_SND_D_OUTPUT ? "n output" : " capture");
			return -EINVAL;
		}
		id = (uint32_t)forced_id;
	} else {
		for (uint32_t i = 0; i < count; i++) {
			if (data->pcm_info[i].direction == direction) {
				id = i;
				break;
			}
		}
	}

	if (id == UINT32_MAX) {
		LOG_WRN("no %s stream available", direction == VIRTIO_SND_D_OUTPUT ? "playback"
										  : "capture");
		return -ENODEV;
	}

	info = &data->pcm_info[id];

	stream->stream_id = id;
	stream->formats = sys_le64_to_cpu(info->formats);
	stream->rates = sys_le64_to_cpu(info->rates);
	stream->hw_features = sys_le32_to_cpu(info->features);
	stream->channels_min = info->channels_min;
	stream->channels_max = info->channels_max;
	stream->present = true;

	LOG_DBG("%s stream %u: channels %u..%u, formats 0x%llx, rates 0x%llx",
		direction == VIRTIO_SND_D_OUTPUT ? "TX" : "RX", id, stream->channels_min,
		stream->channels_max, (unsigned long long)stream->formats,
		(unsigned long long)stream->rates);

	return 0;
}

static int vsnd_init(const struct device *dev)
{
	const struct vsnd_config *cfg = dev->config;
	struct vsnd_data *data = dev->data;
	volatile const struct virtio_snd_config *devcfg;
	uint32_t count;
	int ret;

	data->dev = dev;

	k_mutex_init(&data->ctl_mutex);
	k_sem_init(&data->ctl_sem, 0, 1);

	vsnd_stream_init(&data->tx, dev, I2S_DIR_TX);
	vsnd_stream_init(&data->rx, dev, I2S_DIR_RX);

	k_work_queue_start(&data->workq, data->workq_stack,
			   K_KERNEL_STACK_SIZEOF(data->workq_stack),
			   CONFIG_I2S_VIRTIO_SOUND_WORKQ_PRIORITY, NULL);

	devcfg = virtio_get_device_specific_config(cfg->vdev);
	if (devcfg == NULL) {
		LOG_ERR("no device specific configuration");
		return -ENODEV;
	}

	data->n_streams = sys_le32_to_cpu(devcfg->streams);
	if (data->n_streams == 0U) {
		LOG_ERR("device reports no PCM streams");
		return -ENODEV;
	}

	LOG_DBG("jacks %u, streams %u, chmaps %u", sys_le32_to_cpu(devcfg->jacks), data->n_streams,
		sys_le32_to_cpu(devcfg->chmaps));

	/*
	 * No device feature bit is requested: mixer controls are not supported
	 * and asking for VIRTIO_SND_F_CTLS would only grow the config space.
	 * Committing is still mandatory, it also commits VIRTIO_F_VERSION_1.
	 */
	ret = virtio_commit_feature_bits(cfg->vdev);
	if (ret != 0) {
		LOG_ERR("failed to commit feature bits: %d", ret);
		return ret;
	}

	ret = virtio_init_virtqueues(cfg->vdev, VSND_VQ_COUNT, vsnd_enum_queues_cb, (void *)dev);
	if (ret != 0) {
		LOG_ERR("failed to initialize virtqueues: %d", ret);
		return ret;
	}

	virtio_finalize_init(cfg->vdev);

	if ((cfg->dir_mask & VSND_DIR_TX) != 0U) {
		(void)vsnd_stream_setup(&data->tx, CONFIG_I2S_VIRTIO_SOUND_TX_BLOCKS);
	}

	if ((cfg->dir_mask & VSND_DIR_RX) != 0U) {
		(void)vsnd_stream_setup(&data->rx, CONFIG_I2S_VIRTIO_SOUND_RX_BLOCKS);
	}

#ifdef CONFIG_I2S_VIRTIO_SOUND_EVENTQ
	for (uint16_t i = 0; i < ARRAY_SIZE(data->events); i++) {
		data->event_ctx[i].dev = dev;
		data->event_ctx[i].idx = i;

		if (vsnd_post_event(dev, i) != 0) {
			LOG_WRN("failed to queue event buffer %u", i);
			break;
		}
	}
#endif

	count = MIN(data->n_streams, CONFIG_I2S_VIRTIO_SOUND_MAX_STREAMS);
	if (count < data->n_streams) {
		LOG_WRN("only querying %u of %u streams", count, data->n_streams);
	}

	ret = vsnd_query_pcm_info(dev, count);
	if (ret != 0) {
		return ret;
	}

	if ((cfg->dir_mask & VSND_DIR_TX) != 0U && data->tx.n_reqs >= 2U) {
		ret = vsnd_select_stream(dev, &data->tx, count, cfg->tx_stream_id,
					 VIRTIO_SND_D_OUTPUT);
		if (ret == -EINVAL) {
			return ret;
		}
	}

	if ((cfg->dir_mask & VSND_DIR_RX) != 0U && data->rx.n_reqs >= 2U) {
		ret = vsnd_select_stream(dev, &data->rx, count, cfg->rx_stream_id,
					 VIRTIO_SND_D_INPUT);
		if (ret == -EINVAL) {
			return ret;
		}
	}

	if (!data->tx.present && !data->rx.present) {
		LOG_ERR("no usable PCM stream found");
		return -ENODEV;
	}

	return 0;
}

/* stream-direction enum: 0 == "rx", 1 == "tx", 2 == "both" */
#define VSND_DIR_MASK(n)                                                                           \
	((DT_INST_ENUM_IDX_OR(n, stream_direction, 2) == 0)                                        \
		 ? VSND_DIR_RX                                                                     \
		 : ((DT_INST_ENUM_IDX_OR(n, stream_direction, 2) == 1)                             \
			    ? VSND_DIR_TX                                                          \
			    : (VSND_DIR_RX | VSND_DIR_TX)))

#define VSND_INIT(n)                                                                               \
	static struct vsnd_data vsnd_data_##n;                                                     \
	static const struct vsnd_config vsnd_config_##n = {                                        \
		.vdev = DEVICE_DT_GET(DT_PARENT(DT_DRV_INST(n))),                                  \
		.tx_stream_id = DT_INST_PROP_OR(n, tx_stream_id, -1),                              \
		.rx_stream_id = DT_INST_PROP_OR(n, rx_stream_id, -1),                              \
		.dir_mask = VSND_DIR_MASK(n),                                                      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(n, vsnd_init, NULL, &vsnd_data_##n, &vsnd_config_##n, POST_KERNEL,   \
			      CONFIG_I2S_INIT_PRIORITY, &vsnd_api);

DT_INST_FOREACH_STATUS_OKAY(VSND_INIT)
