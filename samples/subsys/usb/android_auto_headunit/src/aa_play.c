/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_play.h"

#include <stdlib.h>

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

/*
 * A phone sends media at the output rate and speech at a third of it, so the
 * speech channels are stretched up and the three are added together.
 */
#define OUT_RATE     48000U
#define OUT_CHANNELS 2U
#define MEDIA_RATE   48000U
#define SPEECH_RATE  16000U
#define MEDIA_UP     (OUT_RATE / MEDIA_RATE)
#define SPEECH_UP    (OUT_RATE / SPEECH_RATE)

/* Twenty milliseconds an output block, as the phone's messages are */
#define OUT_FRAMES     (OUT_RATE / 50U)
#define OUT_BLOCK_SIZE (OUT_FRAMES * OUT_CHANNELS * sizeof(int16_t))
#define OUT_BLOCK_COUNT 4

#define MEDIA_FRAMES  (OUT_FRAMES / MEDIA_UP)
#define SPEECH_FRAMES (OUT_FRAMES / SPEECH_UP)

/* Held back by this much while speech is playing over it, about ten decibels */
/* The codec's volume register counts up from -57 dB in one decibel steps */
#define WM8904_VOLUME_0DB 57


#define MEDIA_DUCK_NUM 80
#define MEDIA_DUCK_DEN 256

/* Half a second of each channel, enough to ride out the phone's bursts */
#define MEDIA_RING_SIZE  (MEDIA_RATE * OUT_CHANNELS * sizeof(int16_t) / 2U)
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

/*
 * The queues are filled from the thread that reads the phone and emptied by
 * the one that feeds the output, and reset from a third place when a channel
 * stops, so they are held while they are touched.
 */
static struct k_mutex queue_lock;

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

	(void)k_mutex_lock(&queue_lock, K_FOREVER);
	put = ring_buf_put(ring, pcm, (uint32_t)len);
	(void)k_mutex_unlock(&queue_lock);

	if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_PLAY_LEVEL_LOG)) {
		static int64_t since;

		if (k_uptime_get() - since >= 1000) {
			LOG_INF("type %d: asked %u, took %u, ring %u of %u, free %u", type,
				(uint32_t)len, put, ring_buf_size_get(ring),
				ring_buf_capacity_get(ring), ring_buf_space_get(ring));
			since = k_uptime_get();
		}
	}

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

	(void)k_mutex_lock(&queue_lock, K_FOREVER);
	if (speech != NULL) {
		ring_buf_reset(&speech->ring);
		speech->tail = 0;
	} else {
		ring_buf_reset(&media_ring);
	}
	(void)k_mutex_unlock(&queue_lock);
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

	(void)k_mutex_lock(&queue_lock, K_FOREVER);
	got = ring_buf_get(&speech->ring, (uint8_t *)in, sizeof(in));
	(void)k_mutex_unlock(&queue_lock);
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

		for (j = 0; j < SPEECH_UP; j++) {
			int32_t s = prev + (step * (int32_t)j) / (int32_t)SPEECH_UP;
			size_t out = (i * SPEECH_UP + j) * OUT_CHANNELS;

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
	static int16_t in[MEDIA_FRAMES * OUT_CHANNELS]
		Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
	static int16_t tail[OUT_CHANNELS];
	uint32_t got;
	size_t i, j, c;

	(void)k_mutex_lock(&queue_lock, K_FOREVER);
	got = ring_buf_get(&media_ring, (uint8_t *)in, sizeof(in));
	(void)k_mutex_unlock(&queue_lock);
	if (got == 0U) {
		tail[0] = 0;
		tail[1] = 0;
		return;
	}
	if (got < sizeof(in)) {
		memset((uint8_t *)in + got, 0, sizeof(in) - got);
	}

	for (i = 0; i < MEDIA_FRAMES; i++) {
		for (c = 0; c < OUT_CHANNELS; c++) {
			int16_t next = (int16_t)sys_le16_to_cpu((uint16_t)in[i * OUT_CHANNELS + c]);
			int32_t step = (int32_t)next - tail[c];

			for (j = 0; j < MEDIA_UP; j++) {
				int32_t s = tail[c] + (step * (int32_t)j) / (int32_t)MEDIA_UP;

				if (duck) {
					s = (s * MEDIA_DUCK_NUM) / MEDIA_DUCK_DEN;
				}
				frame[(i * MEDIA_UP + j) * OUT_CHANNELS + c] += s;
			}
			tail[c] = next;
		}
	}
}

#if defined(CONFIG_SAMPLE_AA_HU_PLAY_TEST_TONE)

#define TONE_HZ 1000U

/*
 * A tone of the head unit's own, to tell an output that is not working from a
 * phone that is not sending anything.
 */
static void mix_tone(int32_t *frame)
{
	/* One period of a sine at a quarter of full scale */
	static const int16_t sine[32] = {
		0, 1561, 3061, 4445, 5657, 6652, 7391, 7846,
		8000, 7846, 7391, 6652, 5657, 4445, 3061, 1561,
		0, -1561, -3061, -4445, -5657, -6652, -7391, -7846,
		-8000, -7846, -7391, -6652, -5657, -4445, -3061, -1561,
	};
	/* Phase in 256ths of a table entry, so a kilohertz lands on a whole step */
	static uint32_t phase;
	const uint32_t step = (32U * 256U * TONE_HZ) / OUT_RATE;
	size_t i;

	for (i = 0; i < OUT_FRAMES; i++) {
		int32_t s = sine[(phase >> 8) & 31U];

		frame[i * OUT_CHANNELS] += s;
		frame[i * OUT_CHANNELS + 1U] += s;
		phase += step;
	}
}
#endif

static void play_thread(void *p1, void *p2, void *p3)
{
	static int32_t frame[OUT_FRAMES * OUT_CHANNELS]
		Z_GENERIC_SECTION(CONFIG_SAMPLE_AA_HU_PLAY_BUFFERS_SECTION) __aligned(4);
	bool started = false;
	uint32_t peak = 0U;
	uint32_t blocks = 0U;
	uint32_t errors = 0U;
	int64_t mark = k_uptime_get();

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
#if defined(CONFIG_SAMPLE_AA_HU_PLAY_TEST_TONE)
		mix_tone(frame);
#endif
		speaking = mix_speech(&guidance, frame);
		speaking |= mix_speech(&system_sound, frame);
		mix_media(frame, speaking);

		out = block;
		for (i = 0; i < ARRAY_SIZE(frame); i++) {
			int32_t s = CLAMP(frame[i], INT16_MIN, INT16_MAX);

			if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_PLAY_LEVEL_LOG)) {
				peak = MAX(peak, (uint32_t)abs(s));
			}
			out[i] = (int16_t)sys_cpu_to_le16((uint16_t)s);
		}

		if (IS_ENABLED(CONFIG_SAMPLE_AA_HU_PLAY_LEVEL_LOG) &&
		    ++blocks >= (OUT_RATE / OUT_FRAMES)) {
			int64_t now = k_uptime_get();

			LOG_INF("Output: peak %u/%d, %u queued, %lld ms per second of "
				"samples, %u write errors",
				peak, INT16_MAX, ring_buf_size_get(&media_ring), now - mark,
				errors);
			peak = 0U;
			blocks = 0U;
			mark = now;
		}

		ret = i2s_write(i2s_dev, block, OUT_BLOCK_SIZE);
		if (ret != 0) {
			/*
			 * A stream that has run dry stops and refuses everything
			 * afterwards, so put it back rather than going quiet for
			 * the rest of the session.
			 */
			LOG_WRN_ONCE("Could not hand the samples to the output (%d)", ret);
			errors++;
			k_mem_slab_free(&out_slab, block);
			(void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DROP);
			started = false;
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
		/*
		 * Measured against the samples leaving the controller, the SAI
		 * runs at four times the rate its clock is described as, so
		 * asking for a quarter of the rate lands on the right one and
		 * puts the master clock where the codec expects it.
		 */
		.frame_clk_freq = OUT_RATE,
		.mem_slab = &out_slab,
		.block_size = OUT_BLOCK_SIZE,
		.timeout = 200,
	};
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = OUT_RATE * 256U,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		/*
		 * The same options mean the opposite here: the codec reads them
		 * as its own role, so it has to be told it is the target or it
		 * drives the clocks against the controller and the converter
		 * never sees a frame.
		 */
		.dai_cfg.i2s = {
			.word_size = 16U,
			.channels = OUT_CHANNELS,
			.format = I2S_FMT_DATA_FORMAT_I2S,
			.options = I2S_OPT_FRAME_CLK_TARGET | I2S_OPT_BIT_CLK_TARGET,
			.frame_clk_freq = OUT_RATE,
			.block_size = OUT_BLOCK_SIZE,
		},
		/* Without a route the codec configures nothing and stays silent */
		.dai_route = AUDIO_ROUTE_PLAYBACK,
	};
	int ret;

	(void)k_mutex_init(&queue_lock);
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

	/*
	 * The driver leaves the amplifier well down; a head unit wants it
	 * where the phone's own level decides how loud things are.
	 */
	{
		const audio_property_value_t vol = {
			.vol = WM8904_VOLUME_0DB + CONFIG_SAMPLE_AA_HU_PLAY_VOLUME_DB,
		};

		ret = audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
					       AUDIO_CHANNEL_ALL, vol);
		if (ret == 0) {
			(void)audio_codec_apply_properties(codec_dev);
		} else {
			LOG_WRN("Could not set the output volume (%d)", ret);
		}
	}

	LOG_INF("Queues: media %u of %u bytes, guidance %u, system %u",
		ring_buf_capacity_get(&media_ring), (uint32_t)sizeof(media_ring_buf),
		ring_buf_capacity_get(&guidance.ring), ring_buf_capacity_get(&system_sound.ring));

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
