/*
 * Copyright (c) 2024 Benjamin Cabé
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <string.h>
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

/* Block duration for one read cycle, in milliseconds */
#define DMIC_SHELL_BLOCK_DURATION_MS 100U

/* Timeout for dmic_read(), in milliseconds */
#define DMIC_SHELL_READ_TIMEOUT_MS  2000

/* Maximum supported configuration: 48 kHz, 2 ch, 32-bit, 100 ms */
#define DMIC_SHELL_MAX_BLOCK_SIZE \
	((48000U * 2U * (32U / 8U) * DMIC_SHELL_BLOCK_DURATION_MS) / 1000U)

#define DMIC_SHELL_BLOCK_COUNT  4U

/* VU-meter bar settings */
#define DMIC_SHELL_VU_BAR_WIDTH   40U
#define DMIC_SHELL_VU_GREEN_PCT   50U
#define DMIC_SHELL_VU_YELLOW_PCT  75U

/* Default VU meter run duration, in milliseconds */
#define DMIC_SHELL_VU_DEF_DURATION_MS 10000U

K_MEM_SLAB_DEFINE_STATIC(dmic_shell_mem_slab,
			  DMIC_SHELL_MAX_BLOCK_SIZE,
			  DMIC_SHELL_BLOCK_COUNT, 4);

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

			if (v < 0) {
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

/**
 * Print a single VU-meter bar for one channel.
 * The bar is split into green (0–50%), yellow (50–75%), and red (75–100%)
 * segments using ANSI colours provided by the shell colour API.
 *
 * Example output:  CH0 |####################          | 60%
 */
static void print_vu_bar(const struct shell *sh, uint8_t channel, int level_pct)
{
	int green_end = (int)(DMIC_SHELL_VU_BAR_WIDTH * DMIC_SHELL_VU_GREEN_PCT / 100U);
	int yellow_end = (int)(DMIC_SHELL_VU_BAR_WIDTH * DMIC_SHELL_VU_YELLOW_PCT / 100U);
	int filled = level_pct * (int)DMIC_SHELL_VU_BAR_WIDTH / 100;
	char seg[DMIC_SHELL_VU_BAR_WIDTH + 1];
	int seg_len;

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
		memset(seg, '#', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_WARNING, "%s", seg);
	}

	/* Red segment */
	if (filled > yellow_end) {
		seg_len = filled - yellow_end;
		memset(seg, '#', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_ERROR, "%s", seg);
	}

	/* Empty (unfilled) part */
	if (filled < (int)DMIC_SHELL_VU_BAR_WIDTH) {
		seg_len = (int)DMIC_SHELL_VU_BAR_WIDTH - filled;
		memset(seg, ' ', seg_len);
		seg[seg_len] = '\0';
		shell_fprintf(sh, SHELL_NORMAL, "%s", seg);
	}

	/* End of bar: clear to end-of-line and newline so the next frame can
	 * overwrite without leaving stale characters.
	 */
	shell_fprintf(sh, SHELL_NORMAL, "| %3d%%\033[K\n", level_pct);
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
		     DMIC_SHELL_BLOCK_DURATION_MS / 1000U;

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
			dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
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

		ret = dmic_read(dev, 0, &buf, &size, DMIC_SHELL_READ_TIMEOUT_MS);
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
		   "<device> [<duration_ms> [<rate_hz> [<channels> [<pcm_width>]]]]")

static int cmd_vu(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	struct pcm_stream_cfg stream = {0};
	struct dmic_cfg cfg = {0};
	uint32_t duration_ms = DMIC_SHELL_VU_DEF_DURATION_MS;
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

	while (k_uptime_get() < deadline) {
		void *buf;
		size_t size;
		int32_t remaining_ms = (int32_t)(deadline - k_uptime_get());

		ret = dmic_read(dev, 0, &buf, &size, DMIC_SHELL_READ_TIMEOUT_MS);
		if (ret < 0) {
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
			int pct = compute_peak_pct(buf, size, pcm_width,
						   channels, ch);
			print_vu_bar(sh, ch, pct);
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
