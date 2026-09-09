/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include "aa_mic.h"

#include <stdlib.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include "src/aa.pb.h"
#include "aa_frame.h"
#include "aa_ids.h"
#include "aa_session.h"

LOG_MODULE_REGISTER(aa_mic, CONFIG_SAMPLE_AA_HU_LOG_LEVEL);

/*
 * Microphone channel. A phone asks for the microphone when it wants speech,
 * which is what makes the voice assistant usable from the head unit, and the
 * samples are sent back as they are captured. The board's digital microphone
 * is read through the DMIC interface.
 */

#if DT_HAS_ALIAS(dmic0)

#define MIC_RATE     16000U
#define MIC_WIDTH    16U
#define MIC_CHANNELS 1U

/*
 * A block is twenty milliseconds of speech: short enough to keep the assistant
 * responsive and long enough not to send a message per handful of samples.
 */
#define MIC_BLOCK_SAMPLES (MIC_RATE / 50U)
#define MIC_BLOCK_SIZE    (MIC_BLOCK_SAMPLES * (MIC_WIDTH / 8U) * MIC_CHANNELS)
/*
 * Half a second of blocks. The driver fills one every twenty milliseconds and
 * gives up for good once it has none left, so this has to outlast the longest
 * the sending thread can be held off by the phone.
 */
#define MIC_BLOCK_COUNT   24

/* Consecutive failed reads after which the capture is restarted */
#define MIC_ERROR_LIMIT   10

/* Restarts after which the microphone is left alone */
#define MIC_RESTART_LIMIT 5

K_MEM_SLAB_DEFINE_STATIC(mic_slab, MIC_BLOCK_SIZE, MIC_BLOCK_COUNT, 4);

static const struct device *mic_dev = DEVICE_DT_GET(DT_ALIAS(dmic0));
static atomic_t capturing;
static int32_t mic_session;

static void send_response(int32_t status)
{
	MicrophoneResponse rsp = MicrophoneResponse_init_zero;
	uint8_t buf[16];
	int n;

	rsp.has_status = true;
	rsp.status = status;
	rsp.has_session_id = true;
	rsp.session_id = mic_session;

	n = aa_pb_encode(buf, sizeof(buf), MicrophoneResponse_fields, &rsp);
	if (n >= 0) {
		(void)aa_msg_send(aa_hu_session_get()->mic_ch, false, AA_AV_MICROPHONE_RESPONSE,
				  buf, (size_t)n);
	}
}

static int mic_start(void)
{
	struct pcm_stream_cfg stream = {
		.pcm_rate = MIC_RATE,
		.pcm_width = MIC_WIDTH,
		.block_size = MIC_BLOCK_SIZE,
		.mem_slab = &mic_slab,
	};
	struct dmic_cfg cfg = {
		.io = {
			.min_pdm_clk_freq = CONFIG_SAMPLE_AA_HU_MIC_CLK_MIN_HZ,
			.max_pdm_clk_freq = CONFIG_SAMPLE_AA_HU_MIC_CLK_MAX_HZ,
			.min_pdm_clk_dc = 40,
			.max_pdm_clk_dc = 60,
		},
		.streams = &stream,
		.channel = {
			.req_num_streams = 1,
			.req_num_chan = MIC_CHANNELS,
			.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT),
		},
	};
	int ret;

	ret = dmic_configure(mic_dev, &cfg);
	if (ret != 0) {
		LOG_ERR("Could not configure the microphone (%d)", ret);
		return ret;
	}

	ret = dmic_trigger(mic_dev, DMIC_TRIGGER_START);
	if (ret != 0) {
		LOG_ERR("Could not start the microphone (%d)", ret);
		return ret;
	}

	return 0;
}

static void mic_stop(void)
{
	if (atomic_cas(&capturing, 1, 0)) {
		(void)dmic_trigger(mic_dev, DMIC_TRIGGER_STOP);
		LOG_INF("Microphone stopped");
	}
}

#if defined(CONFIG_SAMPLE_AA_HU_MIC_LEVEL_LOG)

/*
 * Level of a squared amplitude in dBFS, to about a decibel: 10 * log10(power)
 * relative to a full scale sample squared. Feeding it a square lets the same
 * helper report a peak and a mean, and keeps the arithmetic integer.
 */
static int mic_dbfs(uint32_t power)
{
	/* 10 * log10(1 + n / 8) */
	static const uint8_t frac[8] = {0, 1, 1, 1, 2, 2, 2, 3};
	unsigned int msb;

	if (power == 0U) {
		return -99;
	}

	msb = find_msb_set(power) - 1U;
	power = (msb >= 3U) ? (power >> (msb - 3U)) : (power << (3U - msb));

	return (int)(3U * msb + frac[power & 7U]) - 90;
}

static void mic_level(const int16_t *samples, size_t count)
{
	static uint64_t energy;
	static uint32_t peak;
	static uint32_t clipped;
	static size_t taken;
	size_t i;

	for (i = 0; i < count; i++) {
		uint32_t mag = (uint32_t)abs(samples[i]);

		energy += (uint64_t)mag * mag;
		peak = MAX(peak, mag);
		if (mag >= INT16_MAX) {
			clipped++;
		}
	}

	taken += count;
	if (taken < MIC_RATE) {
		return;
	}

	LOG_INF("Microphone level: peak %d dBFS, mean %d dBFS, %u clipped", mic_dbfs(peak * peak),
		mic_dbfs((uint32_t)(energy / taken)), clipped);

	energy = 0;
	peak = 0;
	clipped = 0;
	taken = 0;
}

#else

static inline void mic_level(const int16_t *samples, size_t count)
{
	ARG_UNUSED(samples);
	ARG_UNUSED(count);
}

#endif /* CONFIG_SAMPLE_AA_HU_MIC_LEVEL_LOG */

/*
 * The driver stops for good when it runs out of buffers, which a phone that
 * holds off the link for long enough will cause. Put it back rather than
 * leaving the assistant listening to silence for the rest of the session.
 */
static int mic_restart(void)
{
	int ret;

	(void)dmic_trigger(mic_dev, DMIC_TRIGGER_STOP);

	ret = mic_start();
	if (ret != 0) {
		return ret;
	}

	LOG_WRN("Microphone restarted");

	return 0;
}

/* A media message is a timestamp followed by the samples, as video is */
static uint8_t mic_msg[MIC_BLOCK_SIZE + sizeof(uint64_t)];

static int send_block(size_t size)
{
	return aa_msg_send(aa_hu_session_get()->mic_ch, false,
			   AA_AV_MEDIA_WITH_TIMESTAMP_INDICATION, mic_msg,
			   size + sizeof(uint64_t));
}

static void mic_thread(void *p1, void *p2, void *p3)
{
	uint32_t errors = 0;
	uint32_t restarts = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		void *block;
		size_t size;
		int ret;

		if (atomic_get(&capturing) == 0) {
			errors = 0;
			restarts = 0;
			k_sleep(K_MSEC(20));
			continue;
		}

		ret = dmic_read(mic_dev, 0, &block, &size, 200);
		if (ret != 0) {
			/*
			 * Reading again straight away would spin against a
			 * controller that has stopped, so wait between
			 * attempts rather than hold the processor.
			 */
			if (++errors >= MIC_ERROR_LIMIT) {
				errors = 0;
				if (++restarts > MIC_RESTART_LIMIT) {
					LOG_WRN("Microphone keeps failing (%d), stopping", ret);
					mic_stop();
				} else if (mic_restart() != 0) {
					mic_stop();
				}
			}
			k_sleep(K_MSEC(20));
			continue;
		}
		errors = 0;
		restarts = 0;

		/*
		 * Copy the samples out and give the buffer straight back: the
		 * driver only has a few of them and fills one every block, so
		 * holding one across a transfer to the phone would starve it.
		 */
		size = MIN(size, (size_t)MIC_BLOCK_SIZE);
		sys_put_be64((uint64_t)k_ticks_to_us_floor64(k_uptime_ticks()), mic_msg);
		memcpy(&mic_msg[sizeof(uint64_t)], block, size);
		mic_level(block, size / sizeof(int16_t));
		k_mem_slab_free(&mic_slab, block);

		if (atomic_get(&capturing) != 0 && send_block(size) != 0) {
			LOG_WRN_ONCE("Could not send the samples");
		}
	}
}

/*
 * Above the thread that decodes video: the microphone has to be emptied every
 * block or the driver runs out of buffers, while a late picture only costs a
 * frame. Emptying it is a copy and a small message, so it takes little from
 * the decoder.
 */
K_THREAD_DEFINE(aa_mic_tid, 3072, mic_thread, NULL, NULL, NULL,
		MAX(0, CONFIG_SAMPLE_AA_HU_RX_THREAD_PRIORITY - 1), 0, 0);

int aa_mic_init(void)
{
	if (!device_is_ready(mic_dev)) {
		LOG_WRN("Microphone not ready, voice input will not work");
		return 0;
	}

	LOG_INF("Microphone ready (%s)", mic_dev->name);

	return 0;
}

void aa_mic_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	MicrophoneRequest req = MicrophoneRequest_init_zero;

	if (msg_id == AA_AV_MEDIA_ACK_INDICATION) {
		return;
	}

	if (msg_id != AA_AV_MICROPHONE_REQUEST) {
		LOG_DBG("Unhandled microphone message 0x%04x", msg_id);
		return;
	}

	if (aa_pb_decode(body, len, MicrophoneRequest_fields, &req) != 0) {
		return;
	}

	if (!req.open) {
		mic_stop();
		send_response(AA_STATUS_OK);
		return;
	}

	if (!device_is_ready(mic_dev)) {
		send_response(-1);
		return;
	}

	mic_session++;
	if (mic_start() != 0) {
		send_response(-1);
		return;
	}

	atomic_set(&capturing, 1);
	LOG_INF("Microphone started, session %d", mic_session);
	send_response(AA_STATUS_OK);
}

void aa_mic_link_down(void)
{
	mic_stop();
}

#else /* no microphone on this board */

int aa_mic_init(void)
{
	return 0;
}

void aa_mic_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
	ARG_UNUSED(body);
	ARG_UNUSED(len);
	LOG_DBG("No microphone for message 0x%04x", msg_id);
}

void aa_mic_link_down(void)
{
}

#endif /* DT_HAS_ALIAS(dmic0) */
