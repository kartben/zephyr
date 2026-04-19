/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <string.h>
#include <math.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/printk.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(dmic_sample);

/*
 * Espressif PDM bit clock is pcm_rate * 64 (see dmic_esp32). Many PDM mics (e.g. MSM261) want
 * ~1.1–4.8 MHz in standard mode; 16 kHz => 1.024 MHz and can sit nearly silent / wrong level.
 */
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
#define DMIC_PCM_RATE_HZ 48000U
#else
#define DMIC_PCM_RATE_HZ 16000U
#endif

#define SAMPLE_BIT_WIDTH CONFIG_SAMPLE_BIT_WIDTH
#define PDM_CTL_IDX      CONFIG_HW_CHANNEL_INDEX
#define BYTES_PER_SAMPLE SAMPLE_BIT_WIDTH / 8
/* Milliseconds to wait for a block to be read. */
#define READ_TIMEOUT     1000

/* Size of a block for 100 ms of audio data. */
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
	(BYTES_PER_SAMPLE * (_sample_rate / 10) * _number_of_channels)

/* Driver will allocate blocks from this slab to receive audio data into them.
 * Application, after getting a given block from the driver and processing its
 * data, needs to free that block.
 */
#define MAX_BLOCK_SIZE BLOCK_SIZE(DMIC_PCM_RATE_HZ, 2)
/*
 * ESP32 DMIC pulls RX buffers from this slab from ISR context. The driver queues up to
 * CONFIG_AUDIO_DMIC_ESP32_RX_BLOCK_COUNT blocks; the slab must cover that plus the
 * buffer currently being DMA-filled and one held by the app, or k_mem_slab_alloc fails,
 * streaming stops, and dmic_read() eventually times out (-EAGAIN).
 */
#if defined(CONFIG_AUDIO_DMIC_ESP32)
#define DMIC_MEM_SLAB_BLOCKS (CONFIG_AUDIO_DMIC_ESP32_RX_BLOCK_COUNT + 4)
#else
#define DMIC_MEM_SLAB_BLOCKS 8
#endif

K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, DMIC_MEM_SLAB_BLOCKS, 4);

/* --- Realtime ANSI VU meter ------------------------------------------------ */

#define VU_BAR_WIDTH   36
/* ~100 ms per block; 50 blocks ~= 5 s of meter */
#define VU_DEMO_BLOCKS 150
#define VU_USE_INPLACE_REDRAW 1

/* UTF-8 block glyphs (terminals with UTF-8 on the serial port). */
#define VU_FILL  "\342\226\210"
#define VU_EMPTY "\342\226\221"

static int32_t pcm_sample_at(const uint8_t *pcm8, size_t off, unsigned int bits_per_sample)
{
	if (bits_per_sample == 16U) {
		int16_t v;

		memcpy(&v, pcm8 + off, sizeof(v));
		return v;
	} else {
		int32_t v;

		memcpy(&v, pcm8 + off, sizeof(v));
		return v;
	}
}

/* 0 dBFS sine RMS in 16-bit PCM (~32768 / sqrt(2)); use for RMS VU scaling. */
#define PCM_FULLSCALE_RMS_16BIT 23170L
#define PCM_FULLSCALE_RMS_32BIT 1518500259L /* INT32_MAX / sqrt(2) rounded */

static int32_t pcm_fullscale_rms(unsigned int bits_per_sample)
{
	return (bits_per_sample == 16U) ? PCM_FULLSCALE_RMS_16BIT : PCM_FULLSCALE_RMS_32BIT;
}

static uint32_t isqrt64(uint64_t x)
{
	uint64_t res = 0;
	uint64_t bit = 1ULL << 62;

	while (bit > x) {
		bit >>= 2;
	}
	while (bit != 0) {
		if (x >= res + bit) {
			x -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}

	return (uint32_t)MIN(res, (uint64_t)UINT32_MAX);
}

/* RMS level after removing DC (more stable VU than sample peaks on PDM). */
static void pcm_channel_rms_dc(const void *pcm, uint32_t size_bytes, unsigned int channels,
			       unsigned int bits_per_sample, int32_t *rms_out)
{
	const unsigned int bps = bits_per_sample / 8U;

	if (channels == 0U || bps == 0U || size_bytes < (uint32_t)(bps * channels)) {
		return;
	}

	if (bits_per_sample != 16U && bits_per_sample != 32U) {
		return;
	}

	const uint8_t *pcm8 = pcm;
	const size_t frame_bytes = (size_t)bps * (size_t)channels;
	const size_t frames = size_bytes / frame_bytes;

	for (unsigned int ch = 0U; ch < channels; ch++) {
		int64_t sum = 0;

		for (size_t f = 0; f < frames; f++) {
			const size_t off = f * frame_bytes + (size_t)ch * bps;

			sum += pcm_sample_at(pcm8, off, bits_per_sample);
		}

		const int32_t mean = (frames > 0) ? (int32_t)(sum / (int64_t)frames) : 0;
		uint64_t sumsq = 0;

		for (size_t f = 0; f < frames; f++) {
			const size_t off = f * frame_bytes + (size_t)ch * bps;
			const int64_t c = (int64_t)pcm_sample_at(pcm8, off, bits_per_sample) - mean;

			sumsq += (uint64_t)(c * c);
		}

		const uint64_t mean_sq = (frames > 0) ? (sumsq / (uint64_t)frames) : 0U;

		rms_out[ch] = (int32_t)isqrt64(mean_sq);
	}
}

/* dBFS relative to 0 dBFS = full-scale sine RMS (ref). Return tenths of a dB. */
static int rms_to_dbfst10(int32_t rms, int32_t ref)
{
	double db;

	if (ref <= 0) {
		return -960;
	}
	if (rms <= 0) {
		return -960;
	}

	db = 20.0 * log10((double)rms / (double)ref);
	if (db > 0.0) {
		db = 0.0;
	}
	if (db < -96.0) {
		db = -96.0;
	}

	return (int)(db * 10.0 + (db >= 0.0 ? 0.5 : -0.5));
}

/*
 * Bar length from dBFS: map a *display window* (quiet floor .. 0 dBFS) to the bar.
 * Linear % of sine RMS was misleading (e.g. ~80% ≈ −2 dBFS — hot, not "quiet baseline").
 */
#define VU_DB_BAR_FLOOR_ST10 (-550) /* −55.0 dBFS → empty */
#define VU_DB_BAR_CEIL_ST10     0  /* 0 dBFS → full */

__maybe_unused static unsigned int vu_bar_filled_from_dbfst10(int dbfst10)
{
	if (dbfst10 <= VU_DB_BAR_FLOOR_ST10) {
		return 0U;
	}
	if (dbfst10 >= VU_DB_BAR_CEIL_ST10) {
		return VU_BAR_WIDTH;
	}

	return (unsigned int)(((int64_t)(dbfst10 - VU_DB_BAR_FLOOR_ST10) * VU_BAR_WIDTH) /
			      (int64_t)(VU_DB_BAR_CEIL_ST10 - VU_DB_BAR_FLOOR_ST10));
}

/* Bar + numbers use this block's RMS (no peak/decay), so speech shows up if PCM varies. */
__maybe_unused static void vu_row_print(bool mono, unsigned int ch_idx, int32_t block_rms,
					int32_t fs)
{
	char buf[512];
	char *p = buf;
	char *const end = buf + sizeof(buf);
	int n;
	const int dbfst10 = rms_to_dbfst10(block_rms, fs);
	const unsigned int filled = vu_bar_filled_from_dbfst10(dbfst10);
	const char *fill_col;
	int dwhole, dfrac, a;
	bool neg_db;

	/* Color from dBFS, not linear % of ref. */
	if (dbfst10 < -300) {
		fill_col = "\033[32m";
	} else if (dbfst10 < -120) {
		fill_col = "\033[33m";
	} else {
		fill_col = "\033[31m";
	}

	if (mono) {
		n = snprintk(p, (size_t)(end - p),
			     "\033[K Mono  \033[1mVU\033[0m \033[90m[\033[0m");
	} else {
		n = snprintk(p, (size_t)(end - p), "\033[K %s  \033[1mVU\033[0m \033[90m[\033[0m",
			     (ch_idx == 0U) ? " L" : " R");
	}
	if (n < 0 || (size_t)n >= (size_t)(end - p)) {
		return;
	}
	p += n;

	for (unsigned int i = 0U; i < filled; i++) {
		n = snprintk(p, (size_t)(end - p), "%s%s", fill_col, VU_FILL);
		if (n < 0 || (size_t)n >= (size_t)(end - p)) {
			return;
		}
		p += n;
	}

	n = snprintk(p, (size_t)(end - p), "\033[90m");
	if (n < 0 || (size_t)n >= (size_t)(end - p)) {
		return;
	}
	p += n;

	for (unsigned int i = filled; i < VU_BAR_WIDTH; i++) {
		n = snprintk(p, (size_t)(end - p), "%s", VU_EMPTY);
		if (n < 0 || (size_t)n >= (size_t)(end - p)) {
			return;
		}
		p += n;
	}

	a = dbfst10;
	neg_db = (a < 0);
	if (neg_db) {
		a = -a;
	}
	dwhole = a / 10;
	dfrac = a % 10;

	n = snprintk(p, (size_t)(end - p),
		     "]\033[0m blk_rms=%d %s%d.%d dBFS\033[0m\n",
		     (int)block_rms, neg_db ? "-" : "", dwhole, dfrac);
	if (n < 0) {
		return;
	}

	printk("%s", buf);
}

static int dmic_run_vu_meter(const struct device *dmic_dev, struct dmic_cfg *cfg, size_t num_blocks)
{
	int ret;
	const unsigned int chans = cfg->channel.req_num_chan;
	const uint32_t block_ms = (1000U * cfg->streams[0].block_size) /
		((SAMPLE_BIT_WIDTH / 8U) * chans * cfg->streams[0].pcm_rate);
	int32_t level[2];
	const int32_t fs = pcm_fullscale_rms(SAMPLE_BIT_WIDTH);

	if (chans > 2U) {
		return -EINVAL;
	}

	ret = dmic_configure(dmic_dev, cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure the driver: %d", ret);
		return ret;
	}

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("START trigger failed: %d", ret);
		return ret;
	}

	LOG_INF("=== DMIC round: PCM %u Hz, %u ch, block=%u B ===",
		cfg->streams[0].pcm_rate, chans, cfg->streams[0].block_size);
	LOG_INF("0 dBFS = sine RMS ref %d; bar spans %d..0 dBFS",
		(int)fs, VU_DB_BAR_FLOOR_ST10 / 10);
	LOG_INF("expected block duration: %u ms", (unsigned int)block_ms);

#if VU_USE_INPLACE_REDRAW
	printk("\033[?25l\033[0m");
#endif

	const uint32_t t_start = k_uptime_get_32();

	for (size_t n = 0U; n < num_blocks; n++) {
		void *buffer;
		uint32_t size;
#if !VU_USE_INPLACE_REDRAW
		uint32_t t_before = k_uptime_get_32();
#endif

		ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("read %u failed: %d", (unsigned int)n, ret);
			dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
#if VU_USE_INPLACE_REDRAW
			printk("\033[?25h\n");
#endif
			return ret;
		}
#if !VU_USE_INPLACE_REDRAW
		uint32_t t_after = k_uptime_get_32();
#endif

		memset(level, 0, sizeof(level));
		pcm_channel_rms_dc(buffer, size, chans, SAMPLE_BIT_WIDTH, level);

#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32) && !VU_USE_INPLACE_REDRAW
		if (n == 0U) {
			const size_t dump = MIN((size_t)size, 32U);
			const uint8_t *b = buffer;

			LOG_HEXDUMP_INF(b, dump, "head");
			if (size >= 64U) {
				LOG_HEXDUMP_INF(b + (size / 2U), dump, "mid");
			}
			if (size >= 32U) {
				LOG_HEXDUMP_INF(b + (size - dump), dump, "tail");
			}
		}
#endif

#if !VU_USE_INPLACE_REDRAW
		/*
		 * Plain scrolling output while we debug. Print size, read_wait
		 * (ms spent blocked in dmic_read), and the RMS/dBFS for each
		 * channel so we can tell if the driver is actually clocked at the
		 * expected PCM rate.
		 */
		if (chans == 1U) {
			const int d0 = rms_to_dbfst10(level[0], fs);

			LOG_INF("blk %3u: size=%u read_wait=%ums  rms=%d (%d.%d dBFS)",
				(unsigned int)n, (unsigned int)size,
				(unsigned int)(t_after - t_before), (int)level[0],
				d0 / 10, (d0 < 0 ? -d0 : d0) % 10);
		} else {
			const int d0 = rms_to_dbfst10(level[0], fs);
			const int d1 = rms_to_dbfst10(level[1], fs);

			LOG_INF("blk %3u: size=%u read_wait=%ums  L=%d (%d.%d dBFS)  R=%d (%d.%d dBFS)",
				(unsigned int)n, (unsigned int)size,
				(unsigned int)(t_after - t_before),
				(int)level[0], d0 / 10, (d0 < 0 ? -d0 : d0) % 10,
				(int)level[1], d1 / 10, (d1 < 0 ? -d1 : d1) % 10);
		}
#endif

		k_mem_slab_free(&mem_slab, buffer);

#if VU_USE_INPLACE_REDRAW
		if (n > 0U) {
			printk("\033[%uA\r", chans);
		}
		for (unsigned int c = 0U; c < chans; c++) {
			vu_row_print(chans == 1U, c, level[c], fs);
		}
#endif
	}

	LOG_INF("=== round done: %u blocks in %ums ===",
		(unsigned int)num_blocks, (unsigned int)(k_uptime_get_32() - t_start));

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);
	if (ret < 0) {
		LOG_ERR("STOP trigger failed: %d", ret);
	}

#if VU_USE_INPLACE_REDRAW
	printk("\033[?25h\n");
#endif

	return ret;
}

int main(void)
{
	const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));
	int ret;

	LOG_INF("DMIC sample");

	if (!device_is_ready(dmic_dev)) {
		LOG_ERR("%s is not ready", dmic_dev->name);
		return 0;
	}

	struct pcm_stream_cfg stream = {
		.pcm_width = SAMPLE_BIT_WIDTH,
		.mem_slab  = &mem_slab,
	};
	struct dmic_cfg cfg = {
		.io =
			{
#if defined(CONFIG_SOC_FAMILY_ESPRESSIF_ESP32)
				.min_pdm_clk_freq = 1100000,
				.max_pdm_clk_freq = 4800000,
#else
				.min_pdm_clk_freq = 1000000,
				.max_pdm_clk_freq = 3500000,
#endif
				.min_pdm_clk_dc = 40,
				.max_pdm_clk_dc = 60,
			},
		.streams = &stream,
		.channel =
			{
				.req_num_streams = 1,
			},
	};

	cfg.channel.req_num_chan = 1;
	/* ESP32 PDM: must match the mic L/SEL pin (low = left slot, high = right). */
	cfg.channel.req_chan_map_lo =
		dmic_build_channel_map(0, PDM_CTL_IDX, PDM_CHAN_LEFT);
	cfg.streams[0].pcm_rate = DMIC_PCM_RATE_HZ;
	cfg.streams[0].block_size =
		BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

	ret = dmic_run_vu_meter(dmic_dev, &cfg, VU_DEMO_BLOCKS);
	if (ret < 0) {
		return 0;
	}

	cfg.channel.req_num_chan = 2;
	cfg.channel.req_chan_map_lo =
		dmic_build_channel_map(0, PDM_CTL_IDX, PDM_CHAN_LEFT) |
		dmic_build_channel_map(1, PDM_CTL_IDX, PDM_CHAN_RIGHT);
	cfg.streams[0].pcm_rate = DMIC_PCM_RATE_HZ;
	cfg.streams[0].block_size =
		BLOCK_SIZE(cfg.streams[0].pcm_rate, cfg.channel.req_num_chan);

	ret = dmic_run_vu_meter(dmic_dev, &cfg, VU_DEMO_BLOCKS);
	if (ret < 0) {
		return 0;
	}

	LOG_INF("Exiting");
	return 0;
}
