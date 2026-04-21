/*
 * Copyright (c) 2024 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/audio/dmic.h>

/* Default DMIC configuration */
#define DMIC_SHELL_DEF_SAMPLE_RATE  16000U
#define DMIC_SHELL_DEF_CHANNELS     1U
#define DMIC_SHELL_DEF_PCM_WIDTH    16U
#define DMIC_SHELL_DEF_CLK_MIN      1000000U
#define DMIC_SHELL_DEF_CLK_MAX      3500000U
#define DMIC_SHELL_DEF_CLK_DC_MIN   40U
#define DMIC_SHELL_DEF_CLK_DC_MAX   60U

#if defined(CONFIG_DMIC_SHELL_MAX_PCM_WIDTH_16)
#define DMIC_SHELL_MAX_PCM_BYTES 2U
#else
#define DMIC_SHELL_MAX_PCM_BYTES 4U
#endif

/* Maximum block size derived from Kconfig slab sizing limits */
#define DMIC_SHELL_MAX_BLOCK_SIZE                                                              \
	((CONFIG_DMIC_SHELL_MAX_SAMPLE_RATE * CONFIG_DMIC_SHELL_MAX_CHANNELS *                 \
	  DMIC_SHELL_MAX_PCM_BYTES * CONFIG_DMIC_SHELL_BLOCK_DURATION_MS) /                    \
	 1000U)

BUILD_ASSERT(DMIC_SHELL_MAX_BLOCK_SIZE > 0U,
	     "DMIC shell max block size must be positive; check DMIC shell Kconfig limits");

/*
 * VU-meter bar sizing.
 *
 * The bar is drawn between a fixed prefix ("CH0 |", 5 chars) and a fixed
 * suffix ("| 100% !\033[K\n", ~12 chars).  We reserve DMIC_SHELL_VU_MARGINS
 * characters for those so the bar fills the rest of the terminal width.
 */
#define DMIC_SHELL_VU_MARGINS     CONFIG_DMIC_SHELL_VU_MARGINS
#define DMIC_SHELL_VU_BAR_MIN     CONFIG_DMIC_SHELL_VU_BAR_MIN

/*
 * dBFS-based colour thresholds expressed as parts per ten thousand of the
 * linear full-scale amplitude (10000 = 100 %):
 *   Green  -> Yellow at -18 dBFS: 10^(-18/20) ~= 0.1259  -> 1259 / 10000
 *   Yellow -> Red    at  -6 dBFS: 10^(-6/20)  ~= 0.5012  -> 5012 / 10000
 */
#define DMIC_SHELL_VU_GREEN_PER10K   1259U
#define DMIC_SHELL_VU_YELLOW_PER10K  5012U

/* Short dmic_read timeout in vu so shell_process() runs often enough for Ctrl+C. */
#define DMIC_SHELL_VU_READ_POLL_MS                                                             \
	MIN(100, CONFIG_DMIC_SHELL_READ_TIMEOUT_MS)

#define DMIC_SHELL_PCM_FULLSCALE_RMS_16BIT 23170L
#define DMIC_SHELL_PCM_FULLSCALE_RMS_32BIT 1518500259L

#if defined(CONFIG_DMIC_SHELL_MONO_PDM_RIGHT)
#define DMIC_SHELL_DEF_MONO_PDM_CHAN PDM_CHAN_RIGHT
#else
#define DMIC_SHELL_DEF_MONO_PDM_CHAN PDM_CHAN_LEFT
#endif

K_MEM_SLAB_DEFINE_STATIC(dmic_shell_mem_slab,
			  DMIC_SHELL_MAX_BLOCK_SIZE,
			  CONFIG_DMIC_SHELL_BLOCK_COUNT, 4);

/* -------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------- */

/**
 * Compute peak amplitude as a percentage (0–100) of the full scale.
 * Supports 16-bit and 32-bit signed PCM, interleaved channels.
 */
static int compute_peak_pct(const void *buf, size_t size, uint8_t pcm_width,
			     uint8_t num_channels, uint8_t channel)
{
	size_t num_samples;
	int32_t peak = 0;
	int64_t max_val;

	if (pcm_width == 16U) {
		const int16_t *s = (const int16_t *)buf;

		num_samples = size / sizeof(int16_t);
		max_val = INT16_MAX;
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t v = (int32_t)s[i];

			if (v < 0) {
				v = -v;
			}
			if (v > peak) {
				peak = v;
			}
		}
	} else if (pcm_width == 32U) {
		const int32_t *s = (const int32_t *)buf;

		num_samples = size / sizeof(int32_t);
		max_val = INT32_MAX;
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t v = s[i];

			/* Avoid undefined behaviour: negating INT32_MIN
			 * overflows; clamp it to INT32_MAX instead.
			 */
			if (v == INT32_MIN) {
				v = INT32_MAX;
			} else if (v < 0) {
				v = -v;
			}
			if (v > peak) {
				peak = v;
			}
		}
	} else {
		return 0;
	}

	return (int)((int64_t)peak * 100 / max_val);
}

static int32_t dmic_shell_pcm_sample_at(const uint8_t *pcm8, size_t off, uint8_t pcm_width)
{
	if (pcm_width == 16U) {
		int16_t v;

		memcpy(&v, pcm8 + off, sizeof(v));
		return v;
	}

	int32_t v;

	memcpy(&v, pcm8 + off, sizeof(v));
	return v;
}

static uint32_t dmic_shell_isqrt64(uint64_t x)
{
	uint64_t res = 0U;
	uint64_t bit = 1ULL << 62;

	while (bit > x) {
		bit >>= 2;
	}

	while (bit != 0U) {
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

static int32_t dmic_shell_pcm_fullscale_rms(uint8_t pcm_width)
{
	return (pcm_width == 16U) ? DMIC_SHELL_PCM_FULLSCALE_RMS_16BIT
				  : DMIC_SHELL_PCM_FULLSCALE_RMS_32BIT;
}

static int compute_rms_pct_dc(const void *buf, size_t size, uint8_t pcm_width,
			      uint8_t num_channels, uint8_t channel)
{
	const uint8_t *pcm8 = buf;
	const uint8_t bps = pcm_width / 8U;
	const size_t frame_bytes = (size_t)bps * num_channels;
	const size_t frames = size / frame_bytes;
	int64_t sum = 0;
	uint64_t sumsq = 0U;
	int32_t ref;
	uint32_t rms;

	if ((pcm_width != 16U && pcm_width != 32U) || num_channels == 0U || channel >= num_channels ||
	    bps == 0U || frames == 0U) {
		return 0;
	}

	for (size_t f = 0; f < frames; f++) {
		const size_t off = f * frame_bytes + ((size_t)channel * bps);

		sum += dmic_shell_pcm_sample_at(pcm8, off, pcm_width);
	}

	const int32_t mean = (int32_t)(sum / (int64_t)frames);

	for (size_t f = 0; f < frames; f++) {
		const size_t off = f * frame_bytes + ((size_t)channel * bps);
		const int64_t centered =
			(int64_t)dmic_shell_pcm_sample_at(pcm8, off, pcm_width) - mean;

		sumsq += (uint64_t)(centered * centered);
	}

	rms = dmic_shell_isqrt64(sumsq / (uint64_t)frames);
	ref = dmic_shell_pcm_fullscale_rms(pcm_width);
	if (ref <= 0) {
		return 0;
	}

	return MIN((int)(((uint64_t)rms * 100U) / (uint64_t)ref), 100);
}

/**
 * Print a single VU-meter bar for one channel.
 *
 * The bar width is derived from the current terminal width so the meter
 * fills the available space. Colour thresholds follow the DC-removed RMS
 * level used by cmd_vu(), mapped to linear full-scale sine RMS:
 *
 *   Green  (safe):     0 % … ~12.6 % linear  (0 dBFS … -18 dBFS)
 *   Yellow (caution):  ~12.6 % … ~50 % linear (-18 dBFS … -6 dBFS)
 *   Red    (overload): ~50 % … 100 % linear   (-6 dBFS … 0 dBFS)
 *
 * A clip indicator ('!') is appended when the computed level reaches
 * 0 dBFS RMS (100 %).
 *
 * Example (80-column terminal, mid-level signal):
 *   CH0 |###########################################               | 63%
 */
static void print_vu_bar(const struct shell *sh, uint8_t channel,
			 int level_pct, int bar_width)
{
	/* Colour transition points in bar-cell coordinates */
	int green_end  = (int)((uint32_t)bar_width * DMIC_SHELL_VU_GREEN_PER10K  / 10000U);
	int yellow_end = (int)((uint32_t)bar_width * DMIC_SHELL_VU_YELLOW_PER10K / 10000U);
	int filled = level_pct * bar_width / 100;
	/*
	 * Static buffer sized for the maximum allowed bar width.  The caller
	 * must ensure bar_width <= CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH, which
	 * cmd_vu() enforces before calling this function.
	 */
	char seg[CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH + 1];
	int seg_len;

	/* Clamp filled to [0, bar_width] */
	if (filled < 0) {
		filled = 0;
	} else if (filled > bar_width) {
		filled = bar_width;
	}

	shell_fprintf(sh, SHELL_NORMAL, "CH%u |", channel);

	/* Green segment */
	seg_len = MIN(filled, green_end);
	if (seg_len > 0) {
		memset(seg, '#', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_INFO, "%s", seg);
	}

	/* Yellow segment */
	if (filled > green_end) {
		seg_len = MIN(filled, yellow_end) - green_end;
		if (seg_len > 0) {
			memset(seg, '#', seg_len);
			seg[seg_len] = '\0';
			shell_fprintf(sh, SHELL_WARNING, "%s", seg);
		}
	}

	/* Red segment */
	if (filled > yellow_end) {
		seg_len = filled - yellow_end;
		memset(seg, '#', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_ERROR, "%s", seg);
	}

	/* Empty (unfilled) part */
	if (filled < bar_width) {
		seg_len = bar_width - filled;
		memset(seg, ' ', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_NORMAL, "%s", seg);
	}

	/* End of bar: level, optional clip indicator, clear to end of line */
	if (level_pct >= 100) {
		shell_fprintf(sh, SHELL_ERROR, "| %3d%% !\033[K\n", level_pct);
	} else {
		shell_fprintf(sh, SHELL_NORMAL, "| %3d%%\033[K\n", level_pct);
	}
}

/**
 * Build and apply a DMIC configuration.
 *
 * @param sh          Shell instance (for error messages).
 * @param dev         DMIC device.
 * @param sample_rate PCM sample rate in Hz.
 * @param channels    Number of channels (1 or 2).
 * @param pcm_width   Sample width in bits (16 or 32).
 * @param stream      Caller-supplied pcm_stream_cfg to fill in.
 * @param cfg         Caller-supplied dmic_cfg to fill in.
 *
 * @return 0 on success, negative errno on failure.
 */
static int dmic_shell_apply_config(const struct shell *sh,
				   const struct device *dev,
				   uint32_t sample_rate, uint8_t channels,
				   uint8_t pcm_width,
				   struct pcm_stream_cfg *stream,
				   struct dmic_cfg *cfg)
{
	uint32_t block_size;

	block_size = sample_rate * channels * (pcm_width / 8U) *
		     (uint32_t)CONFIG_DMIC_SHELL_BLOCK_DURATION_MS / 1000U;

	if (block_size > DMIC_SHELL_MAX_BLOCK_SIZE) {
		shell_error(sh,
			    "Block size %u exceeds the shell maximum of %u bytes",
			    block_size, DMIC_SHELL_MAX_BLOCK_SIZE);
		return -EINVAL;
	}

	stream->pcm_rate  = sample_rate;
	stream->pcm_width = pcm_width;
	stream->block_size = block_size;
	stream->mem_slab   = &dmic_shell_mem_slab;

	cfg->io.min_pdm_clk_freq = DMIC_SHELL_DEF_CLK_MIN;
	cfg->io.max_pdm_clk_freq = DMIC_SHELL_DEF_CLK_MAX;
	cfg->io.min_pdm_clk_dc   = DMIC_SHELL_DEF_CLK_DC_MIN;
	cfg->io.max_pdm_clk_dc   = DMIC_SHELL_DEF_CLK_DC_MAX;

	cfg->streams = stream;
	cfg->channel.req_num_streams = 1U;
	cfg->channel.req_num_chan    = channels;

	if (channels == 1U) {
		cfg->channel.req_chan_map_lo =
			dmic_build_channel_map(0, 0, DMIC_SHELL_DEF_MONO_PDM_CHAN);
	} else {
		cfg->channel.req_chan_map_lo =
			dmic_build_channel_map(0, 0, PDM_CHAN_LEFT) |
			dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
	}

	return dmic_configure(dev, cfg);
}

/* -------------------------------------------------------------------------
 * Shell command handlers
 * ---------------------------------------------------------------------- */

#define DMIC_READ_HELP \
	SHELL_HELP("Read audio blocks and print peak levels per channel", \
		   "<device> [<count> [<rate_hz> [<channels> [<pcm_width>]]]]")

static int cmd_read(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	struct pcm_stream_cfg stream = {0};
	struct dmic_cfg cfg = {0};
	uint32_t count      = 5U;
	uint32_t sample_rate = DMIC_SHELL_DEF_SAMPLE_RATE;
	uint8_t  channels   = DMIC_SHELL_DEF_CHANNELS;
	uint8_t  pcm_width  = DMIC_SHELL_DEF_PCM_WIDTH;
	int ret;

	dev = shell_device_get_binding(argv[1]);
	if (!dev) {
		shell_error(sh, "DMIC device '%s' not found", argv[1]);
		return -ENODEV;
	}

	if (argc >= 3) {
		count = (uint32_t)strtoul(argv[2], NULL, 0);
	}
	if (argc >= 4) {
		sample_rate = (uint32_t)strtoul(argv[3], NULL, 0);
	}
	if (argc >= 5) {
		channels = (uint8_t)strtoul(argv[4], NULL, 0);
		if (channels < 1U || channels > 2U) {
			shell_error(sh, "Channels must be 1 or 2");
			return -EINVAL;
		}
	}
	if (argc >= 6) {
		pcm_width = (uint8_t)strtoul(argv[5], NULL, 0);
		if (pcm_width != 16U && pcm_width != 32U) {
			shell_error(sh, "PCM width must be 16 or 32");
			return -EINVAL;
		}
	}

	ret = dmic_shell_apply_config(sh, dev, sample_rate, channels, pcm_width,
				      &stream, &cfg);
	if (ret < 0) {
		shell_error(sh, "Failed to configure DMIC: %d", ret);
		return ret;
	}

	ret = dmic_trigger(dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		shell_error(sh, "Failed to start DMIC: %d", ret);
		return ret;
	}

	shell_print(sh, "Reading %u blocks at %u Hz, %u ch, %u-bit:",
		    count, sample_rate, channels, pcm_width);

	for (uint32_t i = 0; i < count; i++) {
		void *buf;
		size_t size;

		ret = dmic_read(dev, 0, &buf, &size, CONFIG_DMIC_SHELL_READ_TIMEOUT_MS);
		if (ret < 0) {
			shell_error(sh, "Block %u: read failed: %d", i, ret);
			break;
		}

		shell_fprintf(sh, SHELL_NORMAL, "  block %u (%u bytes):", i, size);
		for (uint8_t ch = 0; ch < channels; ch++) {
			int pct = compute_peak_pct(buf, size, pcm_width,
						   channels, ch);
			shell_fprintf(sh, SHELL_NORMAL, "  CH%u peak=%3d%%", ch, pct);
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");

		k_mem_slab_free(&dmic_shell_mem_slab, buf);
	}

	(void)dmic_trigger(dev, DMIC_TRIGGER_STOP);
	return ret < 0 ? ret : 0;
}

#define DMIC_VU_HELP \
	SHELL_HELP("Show a live VU meter (green/yellow/red) for the DMIC input", \
		   "<device> [<duration_ms> [<rate_hz> [<channels> [<pcm_width>]]]]; " \
		   "Ctrl+C stops early")

static int cmd_vu(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	struct pcm_stream_cfg stream = {0};
	struct dmic_cfg cfg = {0};
	uint32_t duration_ms = (uint32_t)CONFIG_DMIC_SHELL_VU_DEFAULT_DURATION_MS;
	uint32_t sample_rate = DMIC_SHELL_DEF_SAMPLE_RATE;
	uint8_t  channels    = DMIC_SHELL_DEF_CHANNELS;
	uint8_t  pcm_width   = DMIC_SHELL_DEF_PCM_WIDTH;
	int ret;
	bool first_frame = true;

	dev = shell_device_get_binding(argv[1]);
	if (!dev) {
		shell_error(sh, "DMIC device '%s' not found", argv[1]);
		return -ENODEV;
	}

	if (argc >= 3) {
		duration_ms = (uint32_t)strtoul(argv[2], NULL, 0);
	}
	if (argc >= 4) {
		sample_rate = (uint32_t)strtoul(argv[3], NULL, 0);
	}
	if (argc >= 5) {
		channels = (uint8_t)strtoul(argv[4], NULL, 0);
		if (channels < 1U || channels > 2U) {
			shell_error(sh, "Channels must be 1 or 2");
			return -EINVAL;
		}
	}
	if (argc >= 6) {
		pcm_width = (uint8_t)strtoul(argv[5], NULL, 0);
		if (pcm_width != 16U && pcm_width != 32U) {
			shell_error(sh, "PCM width must be 16 or 32");
			return -EINVAL;
		}
	}

	ret = dmic_shell_apply_config(sh, dev, sample_rate, channels, pcm_width,
				      &stream, &cfg);
	if (ret < 0) {
		shell_error(sh, "Failed to configure DMIC: %d", ret);
		return ret;
	}

	ret = dmic_trigger(dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		shell_error(sh, "Failed to start DMIC: %d", ret);
		return ret;
	}

	/* Number of display lines per frame: one per channel plus one status
	 * line.
	 */
	uint8_t display_lines = channels + 1U;
	int64_t deadline = k_uptime_get() + (int64_t)duration_ms;

	/*
	 * Compute bar width from the current terminal width.
	 * DMIC_SHELL_VU_MARGINS accounts for the fixed prefix ("CH0 |", 5 chars)
	 * and suffix ("| 100% !\033[K\n", ~12 chars).
	 */
	uint16_t term_wid = (sh->ctx != NULL)
			    ? sh->ctx->vt100_ctx.cons.terminal_wid
			    : 0U;

	if (term_wid == 0U) {
		term_wid = CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH;
	}
	int bar_width = (int)term_wid - (int)DMIC_SHELL_VU_MARGINS;

	if (bar_width < (int)DMIC_SHELL_VU_BAR_MIN) {
		bar_width = (int)DMIC_SHELL_VU_BAR_MIN;
	}
	/* Guard against exceeding the static segment buffer */
	if (bar_width > (int)CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH) {
		bar_width = (int)CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH;
	}

	while (k_uptime_get() < deadline) {
		void *buf;
		size_t size;
		int32_t remaining_ms = (int32_t)(deadline - k_uptime_get());

		shell_process(sh);

		if (shell_cmd_abort_requested(sh)) {
			shell_print(sh, "VU meter stopped (Ctrl+C)");
			ret = 0;
			break;
		}

		ret = dmic_read(dev, 0, &buf, &size, DMIC_SHELL_VU_READ_POLL_MS);
		if (ret < 0) {
			if (ret == -EAGAIN || ret == -ETIMEDOUT || ret == -ENOMSG) {
				continue;
			}
			shell_error(sh, "Read failed: %d", ret);
			break;
		}

		/* On subsequent frames, move the cursor back up to overwrite
		 * the previous display in place.
		 */
		if (!first_frame) {
			shell_fprintf(sh, SHELL_NORMAL, "\033[%uA",
				      display_lines);
		}
		first_frame = false;

		for (uint8_t ch = 0; ch < channels; ch++) {
			int pct = compute_rms_pct_dc(buf, size, pcm_width, channels, ch);
			print_vu_bar(sh, ch, pct, bar_width);
		}

		/* Status line – cleared to end-of-line after the text */
		if (remaining_ms > 0) {
			shell_fprintf(sh, SHELL_NORMAL,
				      "  %u Hz  %u ch  %u-bit  "
				      "(%d.%d s remaining)\033[K\n",
				      sample_rate, channels, pcm_width,
				      remaining_ms / 1000,
				      (remaining_ms % 1000) / 100);
		} else {
			shell_fprintf(sh, SHELL_NORMAL,
				      "  %u Hz  %u ch  %u-bit\033[K\n",
				      sample_rate, channels, pcm_width);
		}

		k_mem_slab_free(&dmic_shell_mem_slab, buf);
	}

	(void)dmic_trigger(dev, DMIC_TRIGGER_STOP);
	return ret < 0 ? ret : 0;
}

/* -------------------------------------------------------------------------
 * Device name auto-completion
 * ---------------------------------------------------------------------- */

static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_lookup(idx, NULL);

	entry->syntax  = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help    = NULL;
	entry->subcmd  = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

/* -------------------------------------------------------------------------
 * Command registration
 * ---------------------------------------------------------------------- */

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_dmic,
	SHELL_CMD_ARG(read, &dsub_device_name, DMIC_READ_HELP,
		      cmd_read, 2, 4),
	SHELL_CMD_ARG(vu,   &dsub_device_name, DMIC_VU_HELP,
		      cmd_vu,   2, 4),
	SHELL_SUBCMD_SET_END
);
/* clang-format on */

SHELL_CMD_REGISTER(dmic, &sub_dmic, "Digital Microphone commands", NULL);
