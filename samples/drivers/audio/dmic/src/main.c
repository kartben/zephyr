/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#ifdef CONFIG_SHELL
#include <zephyr/shell/shell.h>
#endif

LOG_MODULE_REGISTER(dmic_sample);

#define MAX_SAMPLE_RATE  16000
#define SAMPLE_BIT_WIDTH CONFIG_SAMPLE_BIT_WIDTH
#define PDM_CTL_IDX      CONFIG_HW_CHANNEL_INDEX
#define BYTES_PER_SAMPLE (SAMPLE_BIT_WIDTH / 8)
/* Milliseconds to wait for a block to be read. */
#define READ_TIMEOUT     1000
#define DEFAULT_BLOCKS   (2 * BLOCK_COUNT)
#define MAX_CHANNELS     2

/*
 * Derive the bar width from the terminal width so the VU line fits on one
 * row.  Per-channel overhead is: label "L " (2), "[" (1), "] NNN%" (7) = 10
 * characters per channel, plus a 2-character separator between channels and
 * the trailing "  press any key to stop" (23 characters).  Round down so both
 * channels always get the same width.
 */
#define VU_BAR_OVERHEAD(_ch) (10 * (_ch) + 2 * ((_ch) - 1) + 23)
#define VU_BAR_WIDTH \
	((CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH - VU_BAR_OVERHEAD(MAX_CHANNELS)) / MAX_CHANNELS)

/* Size of a block for 100 ms of audio data. */
#define BLOCK_SIZE(_sample_rate, _number_of_channels) \
	(BYTES_PER_SAMPLE * ((_sample_rate) / 10) * (_number_of_channels))

/* Driver will allocate blocks from this slab to receive audio data into them.
 * Application, after getting a given block from the driver and processing its
 * data, needs to free that block.
 */
#define MAX_BLOCK_SIZE   BLOCK_SIZE(MAX_SAMPLE_RATE, MAX_CHANNELS)
#define BLOCK_COUNT      4
K_MEM_SLAB_DEFINE_STATIC(mem_slab, MAX_BLOCK_SIZE, BLOCK_COUNT, 4);
K_MUTEX_DEFINE(dmic_lock);
BUILD_ASSERT((SAMPLE_BIT_WIDTH == 16) || (SAMPLE_BIT_WIDTH == 32),
	     "This sample supports 16-bit and 32-bit PCM samples only");

static const struct device *const dmic_dev = DEVICE_DT_GET(DT_NODELABEL(dmic_dev));

static void prepare_dmic_cfg(struct dmic_cfg *cfg, struct pcm_stream_cfg *stream, uint32_t sample_rate,
			     uint8_t channels)
{
	*stream = (struct pcm_stream_cfg) {
		.pcm_width = SAMPLE_BIT_WIDTH,
		.mem_slab = &mem_slab,
		.pcm_rate = sample_rate,
		.block_size = BLOCK_SIZE(sample_rate, channels),
	};

	*cfg = (struct dmic_cfg) {
		.io = {
			/* These fields can be used to limit the PDM clock
			 * configurations that the driver is allowed to use
			 * to those supported by the microphone.
			 */
			.min_pdm_clk_freq = 1000000,
			.max_pdm_clk_freq = 3500000,
			.min_pdm_clk_dc = 40,
			.max_pdm_clk_dc = 60,
		},
		.streams = stream,
		.channel = {
			.req_num_streams = 1,
			.req_num_chan = channels,
		},
	};

	cfg->channel.req_chan_map_lo = dmic_build_channel_map(0, PDM_CTL_IDX, PDM_CHAN_LEFT);
	if (channels == 2U) {
		cfg->channel.req_chan_map_lo |=
			dmic_build_channel_map(1, PDM_CTL_IDX, PDM_CHAN_RIGHT);
	}
}

static int do_pdm_transfer(const struct device *dev, struct dmic_cfg *cfg, size_t block_count)
{
	bool started = false;
	int ret;

	LOG_INF("PCM output rate: %u, channels: %u",
		cfg->streams[0].pcm_rate, cfg->channel.req_num_chan);

	ret = dmic_configure(dev, cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure the driver: %d", ret);
		return ret;
	}

	ret = dmic_trigger(dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("START trigger failed: %d", ret);
		return ret;
	}

	started = true;

	for (size_t i = 0; i < block_count; ++i) {
		void *buffer;
		uint32_t size;

		ret = dmic_read(dev, 0, &buffer, &size, READ_TIMEOUT);
		if (ret < 0) {
			LOG_ERR("%zu - read failed: %d", i, ret);
			break;
		}

		LOG_INF("%zu - got buffer %p of %u bytes", i, buffer, size);

		k_mem_slab_free(&mem_slab, buffer);
	}

	if (started) {
		int stop_ret = dmic_trigger(dev, DMIC_TRIGGER_STOP);

		if ((ret == 0) && (stop_ret < 0)) {
			ret = stop_ret;
		}
		if (stop_ret < 0) {
			LOG_ERR("STOP trigger failed: %d", stop_ret);
		}
	}

	return ret;
}

#ifdef CONFIG_SHELL
struct dmic_vu_ctx {
	struct k_thread thread;
	const struct shell *sh;
	struct dmic_cfg cfg;
	struct pcm_stream_cfg stream;
	atomic_t active;
	atomic_t stop_requested;
};

K_THREAD_STACK_DEFINE(dmic_vu_stack, 2048);
static struct dmic_vu_ctx dmic_vu_ctx;

static int parse_u32_arg(const struct shell *sh, const char *arg, const char *name, uint32_t *value)
{
	char *endptr;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(arg, &endptr, 0);
	/* parsed may be wider than uint32_t on 64-bit hosts. */
	if ((errno != 0) || (endptr == arg) || (*endptr != '\0') || (parsed > UINT32_MAX)) {
		shell_error(sh, "Invalid %s: %s", name, arg);
		return -EINVAL;
	}

	*value = (uint32_t)parsed;
	return 0;
}

static int parse_rate_channels_args(const struct shell *sh, const char *rate_arg,
				    const char *channels_arg, uint32_t *rate, uint8_t *channels);

static int parse_capture_args(const struct shell *sh, size_t argc, char **argv, uint32_t *blocks,
			      uint32_t *rate, uint8_t *channels)
{
	const char *rate_arg = NULL;
	const char *channels_arg = NULL;
	uint32_t parsed;
	int ret;

	*blocks = DEFAULT_BLOCKS;
	*rate = MAX_SAMPLE_RATE;
	*channels = 1U;

	if (argc > 1U) {
		ret = parse_u32_arg(sh, argv[1], "block count", &parsed);
		if (ret < 0) {
			return ret;
		}

		if (parsed == 0U) {
			shell_error(sh, "Block count must be greater than zero");
			return -EINVAL;
		}

		*blocks = parsed;
	}

	if (argc > 2U) {
		rate_arg = argv[2];
	}

	if (argc > 3U) {
		channels_arg = argv[3];
	}

	return parse_rate_channels_args(sh, rate_arg, channels_arg, rate, channels);
}

static int parse_rate_channels_args(const struct shell *sh, const char *rate_arg,
				    const char *channels_arg, uint32_t *rate, uint8_t *channels)
{
	uint32_t parsed;
	int ret;

	*rate = MAX_SAMPLE_RATE;
	*channels = 1U;

	if (rate_arg != NULL) {
		ret = parse_u32_arg(sh, rate_arg, "sample rate", &parsed);
		if (ret < 0) {
			return ret;
		}

		if ((parsed == 0U) || (parsed > MAX_SAMPLE_RATE)) {
			shell_error(sh, "Sample rate must be in the range 1-%u", MAX_SAMPLE_RATE);
			return -EINVAL;
		}

		*rate = parsed;
	}

	if (channels_arg != NULL) {
		ret = parse_u32_arg(sh, channels_arg, "channel count", &parsed);
		if (ret < 0) {
			return ret;
		}

		if ((parsed == 0U) || (parsed > MAX_CHANNELS)) {
			shell_error(sh, "Channel count must be 1 or 2");
			return -EINVAL;
		}

		*channels = (uint8_t)parsed;
	}

	return 0;
}

static int parse_vu_args(const struct shell *sh, size_t argc, char **argv, uint32_t *rate,
			 uint8_t *channels)
{
	return parse_rate_channels_args(sh, (argc > 1U) ? argv[1] : NULL,
					(argc > 2U) ? argv[2] : NULL, rate, channels);
}

static void measure_peak_levels(const void *buffer, uint32_t size, uint8_t channels,
				uint8_t levels[MAX_CHANNELS])
{
	uint64_t peaks[MAX_CHANNELS] = { 0U };
	uint64_t full_scale = 1U;
	size_t sample_count = size / BYTES_PER_SAMPLE;

	if (SAMPLE_BIT_WIDTH == 16) {
		const int16_t *samples = buffer;

		full_scale = INT16_MAX;

		for (size_t i = 0; i < sample_count; ++i) {
			int64_t sample = samples[i];
			uint64_t magnitude = (uint64_t)llabs(sample);
			uint8_t channel = i % channels;

			peaks[channel] = MAX(peaks[channel], magnitude);
		}
	} else if (SAMPLE_BIT_WIDTH == 32) {
		const int32_t *samples = buffer;

		full_scale = INT32_MAX;

		for (size_t i = 0; i < sample_count; ++i) {
			int64_t sample = samples[i];
			uint64_t magnitude = (uint64_t)llabs(sample);
			uint8_t channel = i % channels;

			peaks[channel] = MAX(peaks[channel], magnitude);
		}
	}

	for (uint8_t i = 0U; i < channels; ++i) {
		uint64_t percent = (peaks[i] * 100ULL) / full_scale;

		levels[i] = (uint8_t)MIN(percent, 100ULL);
	}
}

static void render_bar(const struct shell *sh, const char *label, uint8_t level)
{
	uint32_t filled = (level * VU_BAR_WIDTH) / 100U;
	/* Color thresholds derived from standard dBFS zones:
	 *   green  : below -18 dBFS  (linear ~13 % of full scale)
	 *   yellow : -18 dBFS to -6 dBFS  (linear ~50 % of full scale)
	 *   red    : above -6 dBFS
	 */
	uint32_t green_limit = (VU_BAR_WIDTH * 13U) / 100U;
	uint32_t yellow_limit = (VU_BAR_WIDTH * 50U) / 100U;

	shell_fprintf(sh, SHELL_NORMAL, "%s [", label);

	for (uint32_t i = 0U; i < VU_BAR_WIDTH; ++i) {
		enum shell_vt100_color color = SHELL_NORMAL;
		char bar = '.';

		if (i < filled) {
			bar = '#';
			if (i < green_limit) {
				color = SHELL_INFO;
			} else if (i < yellow_limit) {
				color = SHELL_WARNING;
			} else {
				color = SHELL_ERROR;
			}
		}

		shell_fprintf(sh, color, "%c", bar);
	}

	shell_fprintf(sh, SHELL_NORMAL, "] %3u%%", level);
}

static void render_vu_meter(const struct shell *sh, uint8_t channels, uint8_t levels[MAX_CHANNELS])
{
	shell_fprintf(sh, SHELL_NORMAL, "\r\033[2K");
	render_bar(sh, "L", levels[0]);

	if (channels == 2U) {
		shell_fprintf(sh, SHELL_NORMAL, "  ");
		render_bar(sh, "R", levels[1]);
	}

	shell_fprintf(sh, SHELL_NORMAL, "  press any key to stop");
}

static void dmic_vu_bypass_cb(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(user_data);

	atomic_set(&dmic_vu_ctx.stop_requested, 1);
}

static void finish_vu_meter(int ret)
{
	const struct shell *sh = dmic_vu_ctx.sh;

	shell_set_bypass(sh, NULL, NULL);
	shell_fprintf(sh, SHELL_NORMAL, "\r\033[2K");
	if (ret < 0) {
		shell_error(sh, "VU meter stopped with error: %d", ret);
	} else {
		shell_print(sh, "VU meter stopped");
	}

	atomic_set(&dmic_vu_ctx.active, 0);
}

static void dmic_vu_thread(void *p1, void *p2, void *p3)
{
	uint8_t levels[MAX_CHANNELS] = { 0U };
	bool started = false;
	int ret;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	ret = k_mutex_lock(&dmic_lock, K_NO_WAIT);
	if (ret < 0) {
		finish_vu_meter(ret);
		return;
	}

	ret = dmic_configure(dmic_dev, &dmic_vu_ctx.cfg);
	if (ret < 0) {
		goto out;
	}

	ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		goto out;
	}

	started = true;

	while (!atomic_get(&dmic_vu_ctx.stop_requested)) {
		void *buffer;
		uint32_t size;

		ret = dmic_read(dmic_dev, 0, &buffer, &size, READ_TIMEOUT);
		if (ret < 0) {
			goto out;
		}

		if (!atomic_get(&dmic_vu_ctx.stop_requested)) {
			measure_peak_levels(buffer, size, dmic_vu_ctx.cfg.channel.req_num_chan, levels);
			render_vu_meter(dmic_vu_ctx.sh, dmic_vu_ctx.cfg.channel.req_num_chan, levels);
		}

		k_mem_slab_free(&mem_slab, buffer);
	}

	ret = 0;

out:
	if (started) {
		int stop_ret = dmic_trigger(dmic_dev, DMIC_TRIGGER_STOP);

		if ((ret == 0) && (stop_ret < 0)) {
			ret = stop_ret;
		}
	}

	k_mutex_unlock(&dmic_lock);
	finish_vu_meter(ret);
}

static int cmd_dmic_capture(const struct shell *sh, size_t argc, char **argv)
{
	struct dmic_cfg cfg;
	struct pcm_stream_cfg stream;
	uint32_t blocks;
	uint32_t rate;
	uint8_t channels;
	int ret;

	ret = parse_capture_args(sh, argc, argv, &blocks, &rate, &channels);
	if (ret < 0) {
		return ret;
	}

	ret = k_mutex_lock(&dmic_lock, K_NO_WAIT);
	if (ret < 0) {
		shell_error(sh, "DMIC is busy");
		return ret;
	}

	prepare_dmic_cfg(&cfg, &stream, rate, channels);
	shell_print(sh, "Capturing %u blocks at %u Hz using %u channel(s)",
		    blocks, rate, channels);
	ret = do_pdm_transfer(dmic_dev, &cfg, blocks);
	k_mutex_unlock(&dmic_lock);

	return ret;
}

static int cmd_dmic_vu(const struct shell *sh, size_t argc, char **argv)
{
	uint32_t rate;
	uint8_t channels;
	int ret;

	ret = parse_vu_args(sh, argc, argv, &rate, &channels);
	if (ret < 0) {
		return ret;
	}

	if (atomic_cas(&dmic_vu_ctx.active, 0, 1) == false) {
		shell_error(sh, "VU meter is already running");
		return -EBUSY;
	}

	prepare_dmic_cfg(&dmic_vu_ctx.cfg, &dmic_vu_ctx.stream, rate, channels);
	dmic_vu_ctx.sh = sh;
	atomic_set(&dmic_vu_ctx.stop_requested, 0);

	shell_print(sh, "Starting VU meter at %u Hz using %u channel(s)", rate, channels);
	shell_set_bypass(sh, dmic_vu_bypass_cb, NULL);
	k_thread_create(&dmic_vu_ctx.thread, dmic_vu_stack, K_THREAD_STACK_SIZEOF(dmic_vu_stack),
			dmic_vu_thread, NULL, NULL, NULL, K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&dmic_vu_ctx.thread, "dmic_vu");

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
	dmic_cmds,
	SHELL_CMD_ARG(capture, NULL,
		      "Capture DMIC blocks [blocks] [rate] [channels]",
		      cmd_dmic_capture, 1, 3),
	SHELL_CMD_ARG(vu, NULL,
		      "Run a live VU meter [rate=16000] [channels=1]",
		      cmd_dmic_vu, 1, 2),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(dmic, &dmic_cmds, "DMIC sample shell commands", NULL);
#endif /* CONFIG_SHELL */

int main(void)
{
	struct dmic_cfg cfg;
	struct pcm_stream_cfg stream;
	int ret;

	LOG_INF("DMIC sample");

	if (!device_is_ready(dmic_dev)) {
		LOG_ERR("%s is not ready", dmic_dev->name);
		return 0;
	}

	k_mutex_lock(&dmic_lock, K_FOREVER);

	prepare_dmic_cfg(&cfg, &stream, MAX_SAMPLE_RATE, 1U);
	ret = do_pdm_transfer(dmic_dev, &cfg, DEFAULT_BLOCKS);
	if (ret < 0) {
		goto out;
	}

	prepare_dmic_cfg(&cfg, &stream, MAX_SAMPLE_RATE, 2U);
	ret = do_pdm_transfer(dmic_dev, &cfg, DEFAULT_BLOCKS);
	if (ret < 0) {
		goto out;
	}

	LOG_INF("Use the shell 'dmic capture' and 'dmic vu' commands for interactive capture");

out:
	k_mutex_unlock(&dmic_lock);
	LOG_INF("Exiting");
	return 0;
}
