/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/sys/base64.h>

/* Default DMIC configuration */
#define DMIC_SHELL_DEF_SAMPLE_RATE 16000U
#define DMIC_SHELL_DEF_CHANNELS    1U
#define DMIC_SHELL_DEF_PCM_WIDTH   16U

/* PDM clock constraints applied to every capture */
#define DMIC_SHELL_CLK_MIN    1000000U
#define DMIC_SHELL_CLK_MAX    3500000U
#define DMIC_SHELL_CLK_DC_MIN 40U
#define DMIC_SHELL_CLK_DC_MAX 60U

/*
 * One read cycle holds this many milliseconds of audio. Kept short so each heap-allocated buffer
 * stays small; must divide 1000 evenly so BLOCKS_PER_SEC is exact.
 */
#define DMIC_SHELL_BLOCK_DURATION_MS 50U
#define DMIC_SHELL_BLOCKS_PER_SEC    (1000U / DMIC_SHELL_BLOCK_DURATION_MS)

/* Timeout for a single dmic_read(), in milliseconds */
#define DMIC_SHELL_READ_TIMEOUT_MS 2000

/* Number of capture buffers in the pool */
#define DMIC_SHELL_BLOCK_COUNT CONFIG_AUDIO_DMIC_SHELL_BLOCK_COUNT

/*
 * Capture pool, carved from the system heap.
 */
static struct k_mem_slab dmic_shell_slab;
static void *dmic_shell_pool;

/* "dmic read" default number of blocks to capture */
#define DMIC_SHELL_READ_DEF_COUNT 5U

/* Level-meter bar geometry, in characters */
#define DMIC_SHELL_VU_BAR_MIN       4
#define DMIC_SHELL_VU_BAR_MAX       40
/*
 * Fixed characters per channel line around the bar, used to size the bar to the terminal width:
 * "CHx [" (5) + "] " (2) + "NNNN.N dBFS" (11) + " !" clip flag (2).
 */
#define DMIC_SHELL_VU_LINE_OVERHEAD 20

/* Up to two interleaved channels (mono or stereo) */
#define DMIC_SHELL_VU_MAX_CHANNELS 2

/* Background level-meter worker thread */
#define DMIC_SHELL_VU_STACK_SIZE      CONFIG_AUDIO_DMIC_SHELL_VU_STACK_SIZE
/* dmic_read() timeout while metering: short so a key press stops promptly */
#define DMIC_SHELL_VU_READ_TIMEOUT_MS 200

/*
 * The meter works in dBFS (0 dBFS = full-scale signed PCM). Samples of either width are normalised
 * to a common Q15 full scale so 16- and 32-bit share one code path:
 *   FS_N  = 2^15  -> full-scale amplitude
 *   FS_SQ = 2^30  -> full-scale power, the 0 dBFS reference for the dB helper
 */
#define DMIC_SHELL_VU_FS_N   (1 << 15)
#define DMIC_SHELL_VU_FS_SQ  (1ULL << 30)
/* A sample at or above this magnitude (~ -0.01 dBFS) counts as clipping. */
#define DMIC_SHELL_VU_CLIP_N (DMIC_SHELL_VU_FS_N - (DMIC_SHELL_VU_FS_N >> 10))

/* The meter spans this dBFS window; truly silent input shows "-inf". */
#define DMIC_SHELL_VU_FLOOR_DBFS_T  (-600) /* -60.0 dBFS */
/* Conventional digital-audio colour zones, in tenths of a dB. */
#define DMIC_SHELL_VU_GREEN_DBFS_T  (-180) /* green below -18 dBFS */
#define DMIC_SHELL_VU_YELLOW_DBFS_T (-60)  /* yellow -18..-6, red above -6 dBFS */

/*
 * Peak-hold ballistics.
 * After a new peak, the '|' tick is held flat for CONFIG_AUDIO_DMIC_SHELL_VU_PEAK_HOLD_MS, then
 * falls at DMIC_SHELL_VU_HOLD_DECAY_T tenths of a dB per block until it reaches the floor.
 * The clip '!' stays lit for CONFIG_AUDIO_DMIC_SHELL_VU_CLIP_HOLD_MS after the last clipped sample.
 */
#define DMIC_SHELL_VU_HOLD_BLOCKS                                                                  \
	(CONFIG_AUDIO_DMIC_SHELL_VU_PEAK_HOLD_MS / DMIC_SHELL_BLOCK_DURATION_MS)
#define DMIC_SHELL_VU_HOLD_DECAY_T 5
#define DMIC_SHELL_VU_CLIP_HOLD_BLOCKS                                                             \
	(CONFIG_AUDIO_DMIC_SHELL_VU_CLIP_HOLD_MS / DMIC_SHELL_BLOCK_DURATION_MS)

/* "dmic dump" defaults and limits */
#define DMIC_SHELL_DUMP_DEF_SECONDS 2U
#define DMIC_SHELL_DUMP_MAX_SECONDS 10U
/* Encode 48 source bytes (= 64 base64 chars) per line */
#define DMIC_SHELL_DUMP_CHUNK       48U
#define DMIC_SHELL_DUMP_B64         ((DMIC_SHELL_DUMP_CHUNK / 3U) * 4U)

static int parse_audio_params(const struct shell *sh, size_t argc, char *argv[], size_t first,
			      uint32_t *rate, uint8_t *channels, uint8_t *pcm_width)
{
	if (argc > first) {
		*rate = (uint32_t)strtoul(argv[first], NULL, 0);
	}
	if (argc > first + 1) {
		unsigned long val = strtoul(argv[first + 1], NULL, 0);

		if (val < 1UL || val > 2UL) {
			shell_error(sh, "Channels must be 1 or 2");
			return -EINVAL;
		}
		*channels = (uint8_t)val;
	}
	if (argc > first + 2) {
		unsigned long val = strtoul(argv[first + 2], NULL, 0);

		if (val != 16UL && val != 32UL) {
			shell_error(sh, "PCM width must be 16 or 32");
			return -EINVAL;
		}
		*pcm_width = (uint8_t)val;
	}

	return 0;
}

static bool device_is_dmic(const struct device *dev)
{
	return DEVICE_API_IS(dmic, dev);
}

static const struct device *dmic_shell_device(const struct shell *sh, const char *name)
{
	const struct device *dev = shell_device_get_binding(name);

	if (dev == NULL) {
		shell_error(sh, "DMIC device '%s' not found", name);
		return NULL;
	}
	if (!device_is_dmic(dev)) {
		shell_error(sh, "'%s' is not a DMIC device", name);
		return NULL;
	}

	return dev;
}

/*
 * Compute the peak amplitude of one interleaved channel as a percentage (0-100) of full scale.
 * Supports 16-bit and 32-bit signed PCM.
 */
static int compute_peak_pct(const void *buf, size_t size, uint8_t pcm_width, uint8_t num_channels,
			    uint8_t channel)
{
	size_t num_samples;
	int32_t peak = 0;
	int64_t max_val;

	if (pcm_width == 16U) {
		const int16_t *s = buf;

		num_samples = size / sizeof(int16_t);
		max_val = INT16_MAX;
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t v = s[i];

			v = (v < 0) ? -v : v;
			peak = MAX(peak, v);
		}
	} else if (pcm_width == 32U) {
		const int32_t *s = buf;

		num_samples = size / sizeof(int32_t);
		max_val = INT32_MAX;
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t v = s[i];

			/* Negating INT32_MIN is undefined; clamp it instead. */
			if (v == INT32_MIN) {
				v = INT32_MAX;
			} else if (v < 0) {
				v = -v;
			} else {
				/* v >= 0: magnitude unchanged */
			}
			peak = MAX(peak, v);
		}
	} else {
		return 0;
	}

	return (int)((int64_t)peak * 100 / max_val);
}

/* Carve the capture pool out of the system heap and init a slab over it. */
static int dmic_shell_pool_alloc(const struct shell *sh, uint32_t block_size)
{
	size_t slab_block = ROUND_UP(block_size, sizeof(void *));

	dmic_shell_pool = k_malloc(slab_block * DMIC_SHELL_BLOCK_COUNT);
	if (dmic_shell_pool == NULL) {
		shell_error(sh,
			    "Out of heap for %u x %zu byte buffers; raise "
			    "CONFIG_HEAP_MEM_POOL_ADD_SIZE_AUDIO_DMIC_SHELL",
			    DMIC_SHELL_BLOCK_COUNT, slab_block);
		return -ENOMEM;
	}

	return k_mem_slab_init(&dmic_shell_slab, dmic_shell_pool, slab_block,
			       DMIC_SHELL_BLOCK_COUNT);
}

static void dmic_shell_pool_free(void)
{
	k_free(dmic_shell_pool);
	dmic_shell_pool = NULL;
}

/*
 * Allocate the capture pool, configure the DMIC for the requested format and start it.
 * On success the caller owns the running capture and must call dmic_shell_capture_stop() when done;
 * on any failure everything is unwound here.
 */
static int dmic_shell_capture_start(const struct shell *sh, const struct device *dev,
				    uint32_t sample_rate, uint8_t channels, uint8_t pcm_width)
{
	struct pcm_stream_cfg stream = {0};
	struct dmic_cfg cfg = {0};
	uint32_t block_size =
		sample_rate * channels * (pcm_width / 8U) * DMIC_SHELL_BLOCK_DURATION_MS / 1000U;
	int ret;

	ret = dmic_shell_pool_alloc(sh, block_size);
	if (ret < 0) {
		return ret;
	}

	stream.pcm_rate = sample_rate;
	stream.pcm_width = pcm_width;
	stream.block_size = block_size;
	stream.mem_slab = &dmic_shell_slab;

	cfg.io.min_pdm_clk_freq = DMIC_SHELL_CLK_MIN;
	cfg.io.max_pdm_clk_freq = DMIC_SHELL_CLK_MAX;
	cfg.io.min_pdm_clk_dc = DMIC_SHELL_CLK_DC_MIN;
	cfg.io.max_pdm_clk_dc = DMIC_SHELL_CLK_DC_MAX;

	cfg.streams = &stream;
	cfg.channel.req_num_streams = 1U;
	cfg.channel.req_num_chan = channels;
	cfg.channel.req_chan_map_lo = dmic_build_channel_map(0, 0, PDM_CHAN_LEFT);
	if (channels == 2U) {
		cfg.channel.req_chan_map_lo |= dmic_build_channel_map(1, 0, PDM_CHAN_RIGHT);
	}

	ret = dmic_configure(dev, &cfg);
	if (ret < 0) {
		shell_error(sh, "Failed to configure DMIC: %d", ret);
		dmic_shell_pool_free();
		return ret;
	}

	ret = dmic_trigger(dev, DMIC_TRIGGER_START);
	if (ret < 0) {
		shell_error(sh, "Failed to start DMIC: %d", ret);
		dmic_shell_pool_free();
		return ret;
	}

	return 0;
}

/* Stop a running capture and release its heap pool. */
static void dmic_shell_capture_stop(const struct device *dev)
{
	(void)dmic_trigger(dev, DMIC_TRIGGER_STOP);
	dmic_shell_pool_free();
}

/*
 * Per-channel measurements for one captured block, all derived from signed PCM
 * normalised to a Q15 full scale (see DMIC_SHELL_VU_FS_N).
 */
struct dmic_vu_meas {
	int32_t peak_n; /* peak |sample|, 0..FS_N */
	uint32_t clips; /* samples at/above the clip threshold */
};

/*
 * Fixed-point constants for the log2-based dBFS conversion.
 *   LOG2_FRAC_BITS - fractional bits in the Q16 log2 result (1 unit == one octave)
 *   FS_SQ_LOG2     - log2 of the full-scale power reference FS_SQ (= 2^30), i.e. the 0 dBFS point
 *   DB_TENTHS_PER_OCTAVE - tenths of a dB gained per octave of power: 10*log10(2)*10 = 30.103,
 *                          stored scaled by DB_SCALE so the conversion stays in integer math
 */
#define DMIC_SHELL_VU_LOG2_FRAC_BITS       16
#define DMIC_SHELL_VU_LOG2_ONE             (1LL << DMIC_SHELL_VU_LOG2_FRAC_BITS)
#define DMIC_SHELL_VU_FS_SQ_LOG2           30
#define DMIC_SHELL_VU_DB_SCALE             1000
#define DMIC_SHELL_VU_DB_TENTHS_PER_OCTAVE 30103 /* round(30.103 * DMIC_SHELL_VU_DB_SCALE) */

/* Fixed-point base-2 logarithm: returns log2(x) in Q16 for x >= 1. */
static int32_t log2_q16(uint64_t x)
{
	int32_t intpart = 63 - __builtin_clzll(x);
	int32_t frac = 0;
	uint64_t y;

	/* Normalise to a Q30 mantissa in [2^30, 2^31), i.e. [1.0, 2.0). */
	if (intpart >= 30) {
		y = x >> (intpart - 30);
	} else {
		y = x << (30 - intpart);
	}

	/* Recover the fractional bits of log2 by repeated squaring. */
	for (int i = 0; i < DMIC_SHELL_VU_LOG2_FRAC_BITS; i++) {
		y = (y * y) >> 30;
		frac <<= 1;
		if (y >= (1ULL << 31)) {
			y >>= 1;
			frac |= 1;
		}
	}

	return (intpart << DMIC_SHELL_VU_LOG2_FRAC_BITS) | frac;
}

/*
 * Power level in tenths of a dBFS: 10*log10(power / FS_SQ). Returns INT32_MIN (treated as "-inf")
 * for digital silence. The result is <= 0 because power never exceeds full scale. Used for the peak
 * level (amplitude^2).
 */
static int32_t power_dbfs_tenths(uint64_t power)
{
	const int64_t divisor = DMIC_SHELL_VU_LOG2_ONE * DMIC_SHELL_VU_DB_SCALE;
	const int64_t half = divisor / 2;
	int64_t l, num;

	if (power == 0U) {
		return INT32_MIN;
	}

	/* log2(power / FS_SQ) in Q16; <= 0 because power never exceeds full scale. */
	l = (int64_t)log2_q16(power) -
	    ((int64_t)DMIC_SHELL_VU_FS_SQ_LOG2 << DMIC_SHELL_VU_LOG2_FRAC_BITS);
	/* Scale octaves of power to tenths of a dB and undo the Q16 and DB_SCALE factors, rounding
	 * to nearest.
	 */
	num = l * DMIC_SHELL_VU_DB_TENTHS_PER_OCTAVE;
	return (int32_t)((num >= 0) ? (num + half) / divisor : (num - half) / divisor);
}

/*
 * Analyse one interleaved channel of a block: peak amplitude and clip count. Samples are normalized
 * to Q15 full scale, which also sidesteps INT16_MIN / INT32_MIN negation UB (the normalized minimum
 * is -FS_N, whose magnitude +FS_N fits in int32_t).
 */
static void analyze_channel(const void *buf, size_t size, uint8_t pcm_width, uint8_t num_channels,
			    uint8_t channel, struct dmic_vu_meas *m)
{
	uint32_t clips = 0;
	int32_t peak = 0;
	size_t num_samples;

	if (pcm_width == 16U) {
		const int16_t *s = buf;

		num_samples = size / sizeof(int16_t);
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t x = s[i]; /* already Q15: [-FS_N, FS_N) */
			int32_t mag = (x < 0) ? -x : x;

			peak = MAX(peak, mag);
			clips += (mag >= DMIC_SHELL_VU_CLIP_N) ? 1U : 0U;
		}
	} else { /* 32-bit */
		const int32_t *s = buf;

		num_samples = size / sizeof(int32_t);
		for (size_t i = channel; i < num_samples; i += num_channels) {
			int32_t x = (int32_t)(s[i] >> 16); /* normalise to Q15 */
			int32_t mag = (x < 0) ? -x : x;

			peak = MAX(peak, mag);
			clips += (mag >= DMIC_SHELL_VU_CLIP_N) ? 1U : 0U;
		}
	}

	m->peak_n = peak;
	m->clips = clips;
}

/* Format a dBFS value (tenths of a dB) as e.g. "-23.5" or "-inf". */
static void db_fmt(char *buf, size_t buf_size, int32_t tenths)
{
	if (tenths == INT32_MIN) {
		(void)snprintf(buf, buf_size, "-inf");
	} else if (tenths < 0) {
		uint32_t a = (uint32_t)(-tenths);

		(void)snprintf(buf, buf_size, "-%u.%u", a / 10U, a % 10U);
	} else {
		(void)snprintf(buf, buf_size, "%u.%u", (uint32_t)tenths / 10U,
			       (uint32_t)tenths % 10U);
	}
}

/* Colour for one bar cell: the dBFS zone for filled cells and the peak tick, dim for empty cells.
 */
static enum shell_vt100_color bar_cell_color(int i, int filled, int marker, int green_end,
					     int yellow_end)
{
	if (i >= filled && i != marker) {
		return SHELL_NORMAL; /* empty cell */
	}
	if (i < green_end) {
		return SHELL_INFO;
	}
	if (i < yellow_end) {
		return SHELL_WARNING;
	}
	return SHELL_ERROR;
}

static void render_meter_bar(const struct shell *sh, int filled, int marker, int bar_width)
{
	int span = 0 - DMIC_SHELL_VU_FLOOR_DBFS_T;
	int green_end =
		bar_width * (DMIC_SHELL_VU_GREEN_DBFS_T - DMIC_SHELL_VU_FLOOR_DBFS_T) / span;
	int yellow_end =
		bar_width * (DMIC_SHELL_VU_YELLOW_DBFS_T - DMIC_SHELL_VU_FLOOR_DBFS_T) / span;
	int i = 0;

	while (i < bar_width) {
		enum shell_vt100_color color =
			bar_cell_color(i, filled, marker, green_end, yellow_end);
		char run[DMIC_SHELL_VU_BAR_MAX + 1];
		int len = 0;

		while (i < bar_width &&
		       bar_cell_color(i, filled, marker, green_end, yellow_end) == color) {
			char cell;

			if (i == marker) {
				cell = '|';
			} else if (i < filled) {
				cell = '#';
			} else {
				cell = '-';
			}
			run[len++] = cell;
			i++;
		}
		run[len] = '\0';
		shell_fprintf(sh, color, "%s", run);
	}
}

#define DMIC_READ_HELP                                                                             \
	SHELL_HELP("Read audio blocks and print peak levels per channel",                          \
		   "<device> [<count> [<rate_hz> [<channels> [<pcm_width>]]]]")

static int cmd_read(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	uint32_t count = DMIC_SHELL_READ_DEF_COUNT;
	uint32_t rate = DMIC_SHELL_DEF_SAMPLE_RATE;
	uint8_t channels = DMIC_SHELL_DEF_CHANNELS;
	uint8_t pcm_width = DMIC_SHELL_DEF_PCM_WIDTH;
	int ret;

	dev = dmic_shell_device(sh, argv[1]);
	if (dev == NULL) {
		return -ENODEV;
	}

	if (argc >= 3) {
		count = (uint32_t)strtoul(argv[2], NULL, 0);
	}
	ret = parse_audio_params(sh, argc, argv, 3, &rate, &channels, &pcm_width);
	if (ret < 0) {
		return ret;
	}

	ret = dmic_shell_capture_start(sh, dev, rate, channels, pcm_width);
	if (ret < 0) {
		return ret;
	}

	shell_print(sh, "Reading %u blocks at %u Hz, %u ch, %u-bit:", count, rate, channels,
		    pcm_width);

	for (uint32_t i = 0; i < count; i++) {
		void *buf;
		size_t size;

		ret = dmic_read(dev, 0, &buf, &size, DMIC_SHELL_READ_TIMEOUT_MS);
		if (ret < 0) {
			shell_error(sh, "Block %u: read failed: %d", i, ret);
			break;
		}

		shell_fprintf(sh, SHELL_NORMAL, "  block %u (%u bytes):", i, (unsigned int)size);
		for (uint8_t ch = 0; ch < channels; ch++) {
			int pct = compute_peak_pct(buf, size, pcm_width, channels, ch);

			shell_fprintf(sh, SHELL_NORMAL, "  CH%u peak=%3d%%", ch, pct);
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");

		k_mem_slab_free(&dmic_shell_slab, buf);
	}

	dmic_shell_capture_stop(dev);
	return ret < 0 ? ret : 0;
}

struct dmic_vu_ctx {
	struct k_thread thread;
	const struct shell *sh;
	const struct device *dev;
	uint8_t channels;
	uint8_t pcm_width;
	int bar_width;
	uint32_t frames; /* rendered frames; used only to manage in-place redraw */
	/* Per-channel meter state carried across blocks. */
	int32_t hold_tenths[DMIC_SHELL_VU_MAX_CHANNELS];  /* decaying peak-hold, dBFS */
	uint32_t hold_blocks[DMIC_SHELL_VU_MAX_CHANNELS]; /* blocks left holding the peak flat */
	uint32_t clip_blocks[DMIC_SHELL_VU_MAX_CHANNELS]; /* blocks left showing the clip '!' */
	bool thread_started; /* thread object has been created at least once; needs a join */
	atomic_t running;
	atomic_t stop;
};

static K_THREAD_STACK_DEFINE(dmic_vu_stack, DMIC_SHELL_VU_STACK_SIZE);
static struct dmic_vu_ctx dmic_vu_ctx_data;

/*
 * Redraw one meter frame: one line per channel, redrawn in place. Each line is
 *   CHx [#### bar with a '|' peak-hold tick] <peak> dBFS [!]
 * where '#' is the current peak level for the latest block, '|' marks the recent peak hold (held
 * flat then decaying), and a red '!' appears for a short while after the channel clips.
 */
static void render_frame(struct dmic_vu_ctx *ctx, const void *buf, size_t size)
{
	const struct shell *sh = ctx->sh;
	int span = 0 - DMIC_SHELL_VU_FLOOR_DBFS_T;

	/* After the first frame, step the cursor back up over the lines drawn last time so they
	 * are overwritten rather than scrolled.
	 */
	if (ctx->frames > 0U) {
		shell_fprintf(sh, SHELL_NORMAL, "\033[%uA", ctx->channels);
	}

	for (uint8_t ch = 0; ch < ctx->channels; ch++) {
		struct dmic_vu_meas m;
		int32_t peak_t;
		int filled, marker;
		char peak_s[12];

		analyze_channel(buf, size, ctx->pcm_width, ctx->channels, ch, &m);

		peak_t = power_dbfs_tenths((uint64_t)m.peak_n * (uint64_t)m.peak_n);

		/* Peak hold: jump up to a new peak immediately and hold it flat for HOLD_BLOCKS,
		 * then decay gradually and eventually fall back to -inf (it never sticks forever).
		 */
		if (peak_t != INT32_MIN && peak_t >= ctx->hold_tenths[ch]) {
			ctx->hold_tenths[ch] = peak_t;
			ctx->hold_blocks[ch] = DMIC_SHELL_VU_HOLD_BLOCKS;
		} else if (ctx->hold_blocks[ch] > 0U) {
			ctx->hold_blocks[ch]--;
		} else if (ctx->hold_tenths[ch] != INT32_MIN) {
			ctx->hold_tenths[ch] -= DMIC_SHELL_VU_HOLD_DECAY_T;
			if (ctx->hold_tenths[ch] < DMIC_SHELL_VU_FLOOR_DBFS_T) {
				ctx->hold_tenths[ch] = INT32_MIN;
			}
		} else {
			/* no peak hold active */
		}

		/* Clip hold: re-arm on any clipped sample, otherwise count down so the '!' clears
		 * once the input has stayed below the clip threshold for CLIP_HOLD_BLOCKS.
		 */
		if (m.clips > 0U) {
			ctx->clip_blocks[ch] = DMIC_SHELL_VU_CLIP_HOLD_BLOCKS;
		} else if (ctx->clip_blocks[ch] > 0U) {
			ctx->clip_blocks[ch]--;
		} else {
			/* no clip indicator active */
		}

		/*
		 * Map the current peak onto the solid fill and the decaying peak hold onto the '|'
		 * tick.
		 */
		if (peak_t == INT32_MIN) {
			filled = 0;
		} else {
			filled = (int)(((int64_t)peak_t - DMIC_SHELL_VU_FLOOR_DBFS_T) *
				       ctx->bar_width / span);
			filled = CLAMP(filled, 0, ctx->bar_width);
		}
		if (ctx->hold_tenths[ch] == INT32_MIN) {
			marker = -1;
		} else {
			marker =
				(int)(((int64_t)ctx->hold_tenths[ch] - DMIC_SHELL_VU_FLOOR_DBFS_T) *
				      ctx->bar_width / span);
			marker = CLAMP(marker, 0, ctx->bar_width - 1);
		}

		db_fmt(peak_s, sizeof(peak_s), peak_t);

		shell_fprintf(sh, SHELL_NORMAL, "\r\033[2KCH%u [", ch);
		render_meter_bar(sh, filled, marker, ctx->bar_width);
		shell_fprintf(sh, SHELL_NORMAL, "] %6s dBFS", peak_s);
		/* A red '!' flags that the channel has clipped recently. */
		if (ctx->clip_blocks[ch] > 0U) {
			shell_fprintf(sh, SHELL_ERROR, " !");
		}
		shell_fprintf(sh, SHELL_NORMAL, "\n");
	}

	ctx->frames++;
}

/* Any byte received while the meter runs stops it. */
static void dmic_vu_bypass(const struct shell *sh, uint8_t *data, size_t len, void *user_data)
{
	ARG_UNUSED(sh);
	ARG_UNUSED(data);
	ARG_UNUSED(len);
	ARG_UNUSED(user_data);

	atomic_set(&dmic_vu_ctx_data.stop, 1);
}

static void dmic_vu_thread(void *p1, void *p2, void *p3)
{
	struct dmic_vu_ctx *ctx = &dmic_vu_ctx_data;
	bool warned = false;
	int ret = 0;

	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	while (!atomic_get(&ctx->stop)) {
		void *buf;
		size_t size;

		ret = dmic_read(ctx->dev, 0, &buf, &size, DMIC_SHELL_VU_READ_TIMEOUT_MS);
		if (ret == -EAGAIN) {
			/*
			 * No block arrived in time: surface it once and keep trying, staying
			 * responsive to a stop request.
			 */
			if (!warned) {
				shell_warn(ctx->sh, "No audio data yet (read timed out) - check "
						    "the DMIC clock and wiring");
				warned = true;
			}
			continue;
		}
		if (ret < 0) {
			shell_error(ctx->sh, "Read failed: %d", ret);
			break;
		}
		warned = false;

		if (!atomic_get(&ctx->stop)) {
			render_frame(ctx, buf, size);
		}
		k_mem_slab_free(&dmic_shell_slab, buf);
	}

	dmic_shell_capture_stop(ctx->dev);
	shell_set_bypass(ctx->sh, NULL, NULL);
	shell_fprintf(ctx->sh, SHELL_NORMAL, "\r\033[2K");
	shell_print(ctx->sh, "Level meter stopped");
	atomic_clear(&ctx->running);
}

#define DMIC_VU_HELP                                                                               \
	SHELL_HELP("Show a live microphone level meter", "<device> [<rate_hz> [<channels> "        \
							 "[<pcm_width>]]]")

static int cmd_vu(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	uint32_t rate = DMIC_SHELL_DEF_SAMPLE_RATE;
	uint8_t channels = DMIC_SHELL_DEF_CHANNELS;
	uint8_t pcm_width = DMIC_SHELL_DEF_PCM_WIDTH;
	uint16_t term_wid;
	int ret;

	dev = dmic_shell_device(sh, argv[1]);
	if (dev == NULL) {
		return -ENODEV;
	}

	ret = parse_audio_params(sh, argc, argv, 2, &rate, &channels, &pcm_width);
	if (ret < 0) {
		return ret;
	}

	if (!atomic_cas(&dmic_vu_ctx_data.running, 0, 1)) {
		shell_error(sh, "Level meter is already running");
		return -EBUSY;
	}

	/*
	 * Winning the CAS means the previous run (if any) has already cleared "running" and is in
	 * its final unwind. Join it so the kernel has fully torn the thread down before we reuse
	 * the shared thread object and stack below; without this the create could race a thread
	 * that is not quite dead yet.
	 */
	if (dmic_vu_ctx_data.thread_started) {
		k_thread_join(&dmic_vu_ctx_data.thread, K_FOREVER);
	}

	/* Each channel is drawn on its own line, so size the bar to the terminal
	 * width (capped so the numeric readouts stay aligned on wide terminals).
	 */
	term_wid = (sh->ctx != NULL) ? sh->ctx->vt100_ctx.cons.terminal_wid : 0U;
	if (term_wid == 0U) {
		term_wid = CONFIG_SHELL_DEFAULT_TERMINAL_WIDTH;
	}
	dmic_vu_ctx_data.bar_width = CLAMP((int)term_wid - DMIC_SHELL_VU_LINE_OVERHEAD,
					   DMIC_SHELL_VU_BAR_MIN, DMIC_SHELL_VU_BAR_MAX);

	ret = dmic_shell_capture_start(sh, dev, rate, channels, pcm_width);
	if (ret < 0) {
		atomic_clear(&dmic_vu_ctx_data.running);
		return ret;
	}

	dmic_vu_ctx_data.sh = sh;
	dmic_vu_ctx_data.dev = dev;
	dmic_vu_ctx_data.channels = channels;
	dmic_vu_ctx_data.pcm_width = pcm_width;
	dmic_vu_ctx_data.frames = 0U;
	for (uint8_t ch = 0; ch < channels; ch++) {
		dmic_vu_ctx_data.hold_tenths[ch] = INT32_MIN;
		dmic_vu_ctx_data.hold_blocks[ch] = 0U;
		dmic_vu_ctx_data.clip_blocks[ch] = 0U;
	}
	atomic_clear(&dmic_vu_ctx_data.stop);

	shell_print(sh, "Starting level meter at %u Hz, %u ch, %u-bit (press any key to stop)",
		    rate, channels, pcm_width);

	/* Bypass first so a keypress can stop the meter even during start-up. */
	shell_set_bypass(sh, dmic_vu_bypass, NULL);
	k_thread_create(&dmic_vu_ctx_data.thread, dmic_vu_stack,
			K_THREAD_STACK_SIZEOF(dmic_vu_stack), dmic_vu_thread, NULL, NULL, NULL,
			K_PRIO_PREEMPT(8), 0, K_NO_WAIT);
	k_thread_name_set(&dmic_vu_ctx_data.thread, "dmic_vu");
	dmic_vu_ctx_data.thread_started = true;

	return 0;
}

/*
 * Stream `src` as base64, one DMIC_SHELL_DUMP_CHUNK-byte line at a time.
 * Bytes that do not fill a full chunk are kept in @carry and prepended to the next block so the
 * base64 stream stays aligned on 3-byte boundaries until flushed.
 */
static int dump_emit(const struct shell *sh, const uint8_t *src, size_t slen, uint8_t *carry,
		     size_t *carry_len)
{
	uint8_t line[DMIC_SHELL_DUMP_B64 + 1];
	size_t olen;
	int ret;

	if (*carry_len != 0U) {
		size_t take = MIN(DMIC_SHELL_DUMP_CHUNK - *carry_len, slen);

		memcpy(carry + *carry_len, src, take);
		*carry_len += take;
		src += take;
		slen -= take;

		if (*carry_len < DMIC_SHELL_DUMP_CHUNK) {
			return 0;
		}

		ret = base64_encode(line, sizeof(line), &olen, carry, *carry_len);
		if (ret < 0) {
			return ret;
		}
		line[olen] = '\0';
		shell_print(sh, "%s", line);
		*carry_len = 0U;
	}

	while (slen >= DMIC_SHELL_DUMP_CHUNK) {
		ret = base64_encode(line, sizeof(line), &olen, src, DMIC_SHELL_DUMP_CHUNK);
		if (ret < 0) {
			return ret;
		}
		line[olen] = '\0';
		shell_print(sh, "%s", line);
		src += DMIC_SHELL_DUMP_CHUNK;
		slen -= DMIC_SHELL_DUMP_CHUNK;
	}

	if (slen != 0U) {
		memcpy(carry, src, slen);
		*carry_len = slen;
	}

	return 0;
}

static int dump_flush(const struct shell *sh, const uint8_t *carry, size_t carry_len)
{
	uint8_t line[DMIC_SHELL_DUMP_B64 + 1];
	size_t olen;
	int ret;

	if (carry_len == 0U) {
		return 0;
	}

	ret = base64_encode(line, sizeof(line), &olen, carry, carry_len);
	if (ret < 0) {
		return ret;
	}
	line[olen] = '\0';
	shell_print(sh, "%s", line);

	return 0;
}

static void dump_print_instructions(const struct shell *sh, uint32_t rate, uint8_t channels,
				    uint8_t pcm_width)
{
	const char *fmt = (pcm_width == 16U) ? "s16le" : "s32le";

	shell_print(sh, "# --- on your computer ---");
	shell_print(sh, "# Log the serial session to a file (copy-paste drops lines), then:");
	shell_print(sh, "#   sed -n '/BEGIN PCM base64/,/END PCM base64/p' session.log \\");
	shell_print(sh, "#     | grep '^[A-Za-z0-9+/=]\\+$' | base64 -d > audio.raw");
	shell_print(sh, "# Play it:");
	shell_print(sh, "#   ffplay -f %s -ar %u -ac %u audio.raw", fmt, rate, channels);
	shell_print(sh, "# or convert it to a .wav:");
	shell_print(sh, "#   sox -r %u -e signed -b %u -c %u audio.raw audio.wav", rate, pcm_width,
		    channels);
}

#define DMIC_DUMP_HELP                                                                             \
	SHELL_HELP("Dump captured PCM as base64 for offline playback",                             \
		   "<device> [<seconds> [<rate_hz> [<channels> [<pcm_width>]]]]")

static int cmd_dump(const struct shell *sh, size_t argc, char *argv[])
{
	const struct device *dev;
	uint32_t seconds = DMIC_SHELL_DUMP_DEF_SECONDS;
	uint32_t rate = DMIC_SHELL_DEF_SAMPLE_RATE;
	uint8_t channels = DMIC_SHELL_DEF_CHANNELS;
	uint8_t pcm_width = DMIC_SHELL_DEF_PCM_WIDTH;
	uint8_t carry[DMIC_SHELL_DUMP_CHUNK];
	size_t carry_len = 0U;
	uint32_t blocks;
	int ret;

	dev = dmic_shell_device(sh, argv[1]);
	if (dev == NULL) {
		return -ENODEV;
	}

	if (argc >= 3) {
		seconds = (uint32_t)strtoul(argv[2], NULL, 0);
		if (seconds == 0U || seconds > DMIC_SHELL_DUMP_MAX_SECONDS) {
			shell_error(sh, "Duration must be in the range 1-%u seconds",
				    DMIC_SHELL_DUMP_MAX_SECONDS);
			return -EINVAL;
		}
	}
	ret = parse_audio_params(sh, argc, argv, 3, &rate, &channels, &pcm_width);
	if (ret < 0) {
		return ret;
	}

	ret = dmic_shell_capture_start(sh, dev, rate, channels, pcm_width);
	if (ret < 0) {
		return ret;
	}

	blocks = seconds * DMIC_SHELL_BLOCKS_PER_SEC;

	shell_print(sh, "# --- BEGIN PCM base64 (%us, %u Hz, %u ch, %u-bit signed LE) ---", seconds,
		    rate, channels, pcm_width);

	for (uint32_t i = 0; i < blocks; i++) {
		void *buf;
		size_t size;

		ret = dmic_read(dev, 0, &buf, &size, DMIC_SHELL_READ_TIMEOUT_MS);
		if (ret < 0) {
			shell_error(sh, "Block %u: read failed: %d", i, ret);
			break;
		}

		ret = dump_emit(sh, buf, size, carry, &carry_len);
		k_mem_slab_free(&dmic_shell_slab, buf);
		if (ret < 0) {
			shell_error(sh, "base64 encode failed: %d", ret);
			break;
		}
	}

	if (ret == 0) {
		ret = dump_flush(sh, carry, carry_len);
	}

	shell_print(sh, "# --- END PCM base64 ---");

	if (ret == 0) {
		dump_print_instructions(sh, rate, channels, pcm_width);
	}

	dmic_shell_capture_stop(dev);
	return ret < 0 ? ret : 0;
}

static void device_name_get(size_t idx, struct shell_static_entry *entry)
{
	const struct device *dev = shell_device_filter(idx, device_is_dmic);

	entry->syntax = (dev != NULL) ? dev->name : NULL;
	entry->handler = NULL;
	entry->help = NULL;
	entry->subcmd = NULL;
}

SHELL_DYNAMIC_CMD_CREATE(dsub_device_name, device_name_get);

/* clang-format off */
SHELL_STATIC_SUBCMD_SET_CREATE(
	sub_dmic,
	SHELL_CMD_ARG(read, &dsub_device_name, DMIC_READ_HELP, cmd_read, 2, 4),
	SHELL_CMD_ARG(vu,   &dsub_device_name, DMIC_VU_HELP,   cmd_vu,   2, 3),
	SHELL_CMD_ARG(dump, &dsub_device_name, DMIC_DUMP_HELP, cmd_dump, 2, 4),
	SHELL_SUBCMD_SET_END
);
/* clang-format on */

SHELL_CMD_REGISTER(dmic, &sub_dmic, "Digital Microphone commands", NULL);
