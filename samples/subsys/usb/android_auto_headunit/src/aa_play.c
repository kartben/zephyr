/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_play.h"

#include <zephyr/audio/codec.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/ring_buffer.h>

#include "aa_ids.h"

LOG_MODULE_REGISTER(aa_play, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Playing what the phone sends. A phone offers three sinks at two rates:
 * media in stereo at the full rate, and speech and system sounds in mono at a
 * third of it. They play at the same time, so the slower two are stretched to
 * the output rate and the three are added together, with media held down
 * while the assistant or a prompt is speaking, as a car does.
 */

#if DT_HAS_ALIAS(aa_i2s_out)

#define OUT_RATE     48000U
#define OUT_CHANNELS 2U
#define SPEECH_RATE  16000U
#define UPSAMPLE     (OUT_RATE / SPEECH_RATE)

/* One block is twenty milliseconds, as the phone's messages are */
#define OUT_FRAMES     (OUT_RATE / 50U)
#define OUT_BLOCK_SIZE (OUT_FRAMES * OUT_CHANNELS * sizeof(int16_t))
#define OUT_BLOCK_COUNT 4

#define SPEECH_FRAMES (SPEECH_RATE / 50U)

/* Held back by this much while speech is playing over it, about ten decibels */
#define MEDIA_DUCK_NUM 80
#define MEDIA_DUCK_DEN 256

/* Half a second of each channel, enough to ride out the phone's bursts */
#define MEDIA_RING_SIZE  (OUT_RATE * OUT_CHANNELS * sizeof(int16_t) / 2U)
#define SPEECH_RING_SIZE (SPEECH_RATE * sizeof(int16_t) / 2U)

static const struct device *const i2s_dev = DEVICE_DT_GET(DT_ALIAS(aa_i2s_out));
static const struct device *const codec_dev = DEVICE_DT_GET(DT_ALIAS(aa_audio_codec));

/* The output blocks are handed to the DMA, so they stay in internal memory */
K_MEM_SLAB_DEFINE_STATIC(out_slab, OUT_BLOCK_SIZE, OUT_BLOCK_COUNT, 32);

static uint8_t media_ring_buf[MEDIA_RING_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
static uint8_t guidance_ring_buf[SPEECH_RING_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
static uint8_t system_ring_buf[SPEECH_RING_SIZE]
	Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);

struct speech_stream {
	struct ring_buf ring;
	/* Last sample of the previous block, to interpolate across the join */
	int16_t tail;
};

static struct ring_buf media_ring;
static struct speech_stream guidance;
static struct speech_stream system_sound;
static bool running;

static struct speech_stream *speech_of(int32_t type)
{
	if (type == AA_AUDIO_TYPE_GUIDANCE) {
		return &guidance;
	}
	if (type == AA_AUDIO_TYPE_SYSTEM) {
		return &system_sound;
	}

	return NULL;
}

void aa_play_submit(int32_t type, const uint8_t *pcm, size_t len)
{
	struct speech_stream *speech = speech_of(type);
	struct ring_buf *ring = (speech != NULL) ? &speech->ring : &media_ring;
	uint32_t put;

	if (!running || len == 0U) {
		return;
	}

	put = ring_buf_put(ring, pcm, (uint32_t)len);
	if (put < len) {
		/*
		 * The phone is ahead of the output. Dropping the newest keeps
		 * what is already queued playing without a gap.
		 */
		LOG_WRN_ONCE("Audio queue full, dropping samples");
	}
}

void aa_play_flush(int32_t type)
{
	struct speech_stream *speech = speech_of(type);

	if (speech != NULL) {
		ring_buf_reset(&speech->ring);
		speech->tail = 0;
	} else {
		ring_buf_reset(&media_ring);
	}
}

void aa_play_link_down(void)
{
	aa_play_flush(AA_AUDIO_TYPE_MEDIA);
	aa_play_flush(AA_AUDIO_TYPE_GUIDANCE);
	aa_play_flush(AA_AUDIO_TYPE_SYSTEM);
}

/*
 * Stretch a mono block to the output rate and add it to the frame. The rate
 * is an exact multiple, so the samples in between are a straight line from one
 * input sample to the next rather than anything needing a filter.
 */
static bool mix_speech(struct speech_stream *speech, int32_t *frame)
{
	/* Static: a block is far larger than a thread stack wants to carry */
	static int16_t in[SPEECH_FRAMES]
		Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
	uint32_t got;
	size_t i, j;
	int16_t prev;

	got = ring_buf_get(&speech->ring, (uint8_t *)in, sizeof(in));
	if (got < sizeof(in)) {
		/* Short block: the rest is silence rather than a repeat */
		memset((uint8_t *)in + got, 0, sizeof(in) - got);
		if (got == 0U) {
			speech->tail = 0;
			return false;
		}
	}

	prev = speech->tail;
	for (i = 0; i < SPEECH_FRAMES; i++) {
		int16_t next = sys_le16_to_cpu((uint16_t)in[i]);
		int32_t step = (int32_t)next - prev;

		for (j = 0; j < UPSAMPLE; j++) {
			int32_t s = prev + (step * (int32_t)j) / (int32_t)UPSAMPLE;
			size_t out = (i * UPSAMPLE + j) * OUT_CHANNELS;

			frame[out] += s;
			frame[out + 1U] += s;
		}
		prev = next;
	}
	speech->tail = prev;

	return true;
}

static void mix_media(int32_t *frame, bool duck)
{
	static int16_t in[OUT_FRAMES * OUT_CHANNELS]
		Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
	uint32_t got;
	size_t i;

	got = ring_buf_get(&media_ring, (uint8_t *)in, sizeof(in));
	if (got == 0U) {
		return;
	}
	if (got < sizeof(in)) {
		memset((uint8_t *)in + got, 0, sizeof(in) - got);
	}

	for (i = 0; i < ARRAY_SIZE(in); i++) {
		int32_t s = (int16_t)sys_le16_to_cpu((uint16_t)in[i]);

		if (duck) {
			s = (s * MEDIA_DUCK_NUM) / MEDIA_DUCK_DEN;
		}
		frame[i] += s;
	}
}

static void play_thread(void *p1, void *p2, void *p3)
{
	static int32_t frame[OUT_FRAMES * OUT_CHANNELS]
		Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
	bool started = false;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		void *block;
		int16_t *out;
		bool speaking;
		size_t i;
		int ret;

		if (!running) {
			k_sleep(K_MSEC(20));
			continue;
		}

		ret = k_mem_slab_alloc(&out_slab, &block, K_MSEC(200));
		if (ret != 0) {
			continue;
		}

		memset(frame, 0, sizeof(frame));
		speaking = mix_speech(&guidance, frame);
		speaking |= mix_speech(&system_sound, frame);
		mix_media(frame, speaking);

		out = block;
		for (i = 0; i < ARRAY_SIZE(frame); i++) {
			out[i] = (int16_t)sys_cpu_to_le16(
				(uint16_t)CLAMP(frame[i], INT16_MIN, INT16_MAX));
		}

		ret = i2s_write(i2s_dev, block, OUT_BLOCK_SIZE);
		if (ret != 0) {
			LOG_WRN_ONCE("Could not hand the samples to the output (%d)", ret);
			k_mem_slab_free(&out_slab, block);
			k_sleep(K_MSEC(20));
			continue;
		}

		/*
		 * Started once a couple of blocks are queued, so the output
		 * does not run dry between the first and the second.
		 */
		if (!started && k_mem_slab_num_free_get(&out_slab) <= OUT_BLOCK_COUNT - 2) {
			ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
			if (ret != 0) {
				LOG_ERR("Could not start the output (%d)", ret);
			} else {
				started = true;
			}
		}
	}
}

K_THREAD_DEFINE(aa_play_tid, 2048, play_thread, NULL, NULL, NULL,
		MAX(0, CONFIG_SAMPLE_AA_HU_RX_THREAD_PRIORITY - 1), 0, 0);

int aa_play_init(void)
{
	struct i2s_config i2s_cfg = {
		.word_size = 16U,
		.channels = OUT_CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER,
		.frame_clk_freq = OUT_RATE,
		.mem_slab = &out_slab,
		.block_size = OUT_BLOCK_SIZE,
		.timeout = 200,
	};
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = OUT_RATE * 256U,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_cfg.i2s = i2s_cfg,
	};
	int ret;

	ring_buf_init(&media_ring, sizeof(media_ring_buf), media_ring_buf);
	ring_buf_init(&guidance.ring, sizeof(guidance_ring_buf), guidance_ring_buf);
	ring_buf_init(&system_sound.ring, sizeof(system_ring_buf), system_ring_buf);

	if (!device_is_ready(i2s_dev) || !device_is_ready(codec_dev)) {
		LOG_WRN("No audio output, the phone will be heard on the phone");
		return 0;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_TX, &i2s_cfg);
	if (ret != 0) {
		LOG_ERR("Could not configure the output (%d)", ret);
		return 0;
	}

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret != 0) {
		LOG_ERR("Could not configure the codec (%d)", ret);
		return 0;
	}

	audio_codec_start_output(codec_dev);
	running = true;
	LOG_INF("Audio output ready (%s through %s)", i2s_dev->name, codec_dev->name);

	return 0;
}

#else /* nowhere to play */

int aa_play_init(void)
{
	return 0;
}

void aa_play_submit(int32_t type, const uint8_t *pcm, size_t len)
{
	ARG_UNUSED(type);
	ARG_UNUSED(pcm);
	ARG_UNUSED(len);
}

void aa_play_flush(int32_t type)
{
	ARG_UNUSED(type);
}

void aa_play_link_down(void)
{
}

#endif /* DT_HAS_ALIAS(aa_i2s_out) */
