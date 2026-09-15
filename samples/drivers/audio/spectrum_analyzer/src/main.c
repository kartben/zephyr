/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/audio/codec.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/sys/util.h>

#include <arm_math.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(spectrum, LOG_LEVEL_INF);

#define I2S_NODE     DT_NODELABEL(i2s_rx)
#define CODEC_NODE   DT_NODELABEL(audio_codec)
#define DISPLAY_NODE DT_CHOSEN(zephyr_display)

BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(I2S_NODE), "no enabled i2s_rx node");
BUILD_ASSERT(DT_NODE_HAS_STATUS_OKAY(DISPLAY_NODE), "no enabled zephyr,display node");

#define SAMPLE_RATE CONFIG_SAMPLE_RATE
#define CHANNELS    CONFIG_SAMPLE_CHANNELS
#define FFT_SIZE    CONFIG_SAMPLE_FFT_SIZE
#define BANDS       CONFIG_SAMPLE_BANDS
#define BINS        (FFT_SIZE / 2)

/* One captured block holds exactly one transform */
#define BLOCK_SIZE  (FFT_SIZE * CHANNELS * sizeof(int16_t))
#define BLOCK_COUNT 4

/* Lowest frequency the display starts at; below it a small speaker says nothing */
#define BAND_LOW_HZ 80.0f

/* Levels this far below the loudest band leave a bar empty */
#define LEVEL_FLOOR_DB (-72.0f)
#define LEVEL_CEIL_DB  (-6.0f)

/* Bars fall by at most this many pixels per frame, peak markers by one */
#define BAR_FALL_PIXELS 3

#define WIDTH  DT_PROP_OR(DISPLAY_NODE, width, 0)
#define HEIGHT DT_PROP_OR(DISPLAY_NODE, height, 0)

BUILD_ASSERT(WIDTH > 0 && HEIGHT > 0, "the display node must carry width and height properties");
BUILD_ASSERT(HEIGHT % 8 == 0, "the display height must be a whole number of tiles");

static const struct device *const i2s_dev = DEVICE_DT_GET(I2S_NODE);
static const struct device *const display_dev = DEVICE_DT_GET(DISPLAY_NODE);

K_MEM_SLAB_DEFINE_STATIC(rx_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

/* Newest captured frame, handed from the capture thread to the renderer */
static int16_t frame[FFT_SIZE];
static K_MUTEX_DEFINE(frame_lock);
static K_SEM_DEFINE(frame_ready, 0, 1);

static arm_rfft_fast_instance_f32 fft;
static float32_t window[FFT_SIZE];
static float32_t samples[FFT_SIZE];
static float32_t spectrum[FFT_SIZE];
static float32_t magnitude[BINS];

/* First and last bin of each band, filled in by bands_init() */
static uint16_t band_first[BANDS];
static uint16_t band_last[BANDS];

static uint8_t bar[BANDS];
static uint8_t peak[BANDS];

static uint8_t framebuf[WIDTH * HEIGHT / 8];
static bool tile_vertically;
static bool msb_is_first;
static bool lit_bit_is_set;

static void fb_clear(void)
{
	memset(framebuf, lit_bit_is_set ? 0x00 : 0xFF, sizeof(framebuf));
}

static void fb_pixel(uint16_t x, uint16_t y)
{
	size_t idx;
	uint8_t bit;

	if (tile_vertically) {
		idx = (size_t)x + ((size_t)y / 8) * WIDTH;
		bit = y % 8;
	} else {
		idx = ((size_t)x / 8) + (size_t)y * (WIDTH / 8);
		bit = x % 8;
	}

	if (msb_is_first) {
		bit = 7 - bit;
	}

	if (lit_bit_is_set) {
		framebuf[idx] |= BIT(bit);
	} else {
		framebuf[idx] &= ~BIT(bit);
	}
}

static void fb_fill(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
	for (uint16_t dy = 0; dy < h; dy++) {
		for (uint16_t dx = 0; dx < w; dx++) {
			fb_pixel(x + dx, y + dy);
		}
	}
}

/*
 * Spread the bands logarithmically over the spectrum, which is how the ear
 * hears it. Bands are contiguous and never empty, so the lowest ones end up
 * sharing the few bins the transform resolves down there.
 */
static void bands_init(void)
{
	const float32_t bin_hz = (float32_t)SAMPLE_RATE / FFT_SIZE;
	const float32_t top_hz = SAMPLE_RATE / 2.0f;
	uint16_t next = MAX(1, (uint16_t)(BAND_LOW_HZ / bin_hz));

	for (unsigned int b = 0; b < BANDS; b++) {
		float32_t edge_hz = BAND_LOW_HZ * powf(top_hz / BAND_LOW_HZ, (b + 1.0f) / BANDS);
		uint16_t last = (uint16_t)(edge_hz / bin_hz);

		band_first[b] = next;
		band_last[b] = CLAMP(last, next, BINS - 1);
		next = band_last[b] + 1;
	}
}

/* Power summed over a band, as a level relative to a full-scale sine */
static float32_t band_level_db(unsigned int b)
{
	/* Hann halves the amplitude of a tone spread over FFT_SIZE/2 bins */
	const float32_t full_scale = FFT_SIZE / 4.0f;
	float32_t power = 0.0f;

	for (uint16_t i = band_first[b]; i <= band_last[b]; i++) {
		power += magnitude[i] * magnitude[i];
	}

	if (power <= 0.0f) {
		return LEVEL_FLOOR_DB;
	}

	return 10.0f * log10f(power / (full_scale * full_scale));
}

static void render(void)
{
	const uint16_t pitch = WIDTH / BANDS;
	const uint16_t bar_w = MAX(1, pitch - 1);
	const uint16_t margin = (WIDTH - pitch * BANDS) / 2;
	struct display_buffer_descriptor desc = {
		.buf_size = sizeof(framebuf),
		.width = WIDTH,
		.height = HEIGHT,
		.pitch = WIDTH,
	};

	fb_clear();

	for (unsigned int b = 0; b < BANDS; b++) {
		float32_t db = CLAMP(band_level_db(b), LEVEL_FLOOR_DB, LEVEL_CEIL_DB);
		uint8_t level = (uint8_t)((db - LEVEL_FLOOR_DB) * HEIGHT /
					  (LEVEL_CEIL_DB - LEVEL_FLOOR_DB));
		uint16_t x = margin + b * pitch;

		bar[b] = MAX(level, bar[b] > BAR_FALL_PIXELS ? bar[b] - BAR_FALL_PIXELS : 0);
		peak[b] = MAX(bar[b], peak[b] > 0 ? peak[b] - 1 : 0);

		if (bar[b] > 0) {
			fb_fill(x, HEIGHT - bar[b], bar_w, bar[b]);
		}

		if (peak[b] > bar[b]) {
			fb_fill(x, HEIGHT - peak[b], bar_w, 1);
		}
	}

	display_write(display_dev, 0, 0, &desc, framebuf);
}

static void analyze(void)
{
	k_mutex_lock(&frame_lock, K_FOREVER);
	arm_q15_to_float(frame, samples, FFT_SIZE);
	k_mutex_unlock(&frame_lock);

	arm_mult_f32(samples, window, samples, FFT_SIZE);
	arm_rfft_fast_f32(&fft, samples, spectrum, 0);
	arm_cmplx_mag_f32(spectrum, magnitude, BINS);

	/* The transform packs DC and Nyquist into the first complex pair */
	magnitude[0] = 0.0f;
}

static void capture_thread(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	for (;;) {
		const int16_t *pcm;
		void *block;
		size_t size;
		int ret;

		ret = i2s_read(i2s_dev, &block, &size);
		if (ret < 0) {
			LOG_ERR("Capture stopped: %d", ret);
			return;
		}

		pcm = block;
		k_mutex_lock(&frame_lock, K_FOREVER);
		for (unsigned int i = 0; i < FFT_SIZE; i++) {
			frame[i] = pcm[i * CHANNELS];
		}
		k_mutex_unlock(&frame_lock);

		k_mem_slab_free(&rx_slab, block);

		/*
		 * The semaphore holds a single count, so a renderer that falls
		 * behind skips the frames it missed rather than lagging.
		 */
		k_sem_give(&frame_ready);
	}
}

K_THREAD_STACK_DEFINE(capture_stack, 1024);
static struct k_thread capture_tid;

static int codec_start(const struct i2s_config *i2s_cfg)
{
	int ret;

#if DT_NODE_HAS_STATUS_OKAY(CODEC_NODE)
	const struct device *const codec_dev = DEVICE_DT_GET(CODEC_NODE);
	struct audio_codec_cfg codec_cfg = {
		.mclk_freq = 256 * SAMPLE_RATE,
		.dai_type = AUDIO_DAI_TYPE_I2S,
		.dai_route = AUDIO_ROUTE_CAPTURE,
		.dai_cfg.i2s = *i2s_cfg,
	};

	if (!device_is_ready(codec_dev)) {
		LOG_ERR("%s is not ready", codec_dev->name);
		return -ENODEV;
	}

	ret = audio_codec_configure(codec_dev, &codec_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s: %d", codec_dev->name, ret);
		return ret;
	}

	ret = audio_codec_set_property(
		codec_dev, AUDIO_PROPERTY_INPUT_VOLUME, AUDIO_CHANNEL_ALL,
		(audio_property_value_t){.vol = CONFIG_SAMPLE_INPUT_GAIN_DB});
	if (ret < 0) {
		LOG_WRN("Failed to set the input gain: %d", ret);
	}
#else
	ARG_UNUSED(i2s_cfg);
	ret = 0;
#endif

	return ret;
}

static int display_start(void)
{
	struct display_capabilities caps;
	int ret;

	if (!device_is_ready(display_dev)) {
		LOG_ERR("%s is not ready", display_dev->name);
		return -ENODEV;
	}

	display_get_capabilities(display_dev, &caps);

	if (caps.x_resolution != WIDTH || caps.y_resolution != HEIGHT) {
		LOG_ERR("%s is %ux%u, the sample was built for %ux%u", display_dev->name,
			caps.x_resolution, caps.y_resolution, WIDTH, HEIGHT);
		return -ENOTSUP;
	}

	if (caps.current_pixel_format != PIXEL_FORMAT_MONO01 &&
	    caps.current_pixel_format != PIXEL_FORMAT_MONO10) {
		LOG_ERR("%s is not a monochrome display", display_dev->name);
		return -ENOTSUP;
	}

	lit_bit_is_set = caps.current_pixel_format == PIXEL_FORMAT_MONO01;
	tile_vertically = (caps.screen_info & SCREEN_INFO_MONO_VTILED) != 0;
	msb_is_first = (caps.screen_info & SCREEN_INFO_MONO_MSB_FIRST) != 0;

	ret = display_blanking_off(display_dev);
	if (ret < 0) {
		LOG_ERR("Failed to turn the display on: %d", ret);
		return ret;
	}

	return 0;
}

int main(void)
{
	struct i2s_config i2s_cfg = {
		.word_size = 16,
		.channels = CHANNELS,
		.format = I2S_FMT_DATA_FORMAT_I2S,
		.options = 0,
		.frame_clk_freq = SAMPLE_RATE,
		.mem_slab = &rx_slab,
		.block_size = BLOCK_SIZE,
		.timeout = 2000,
	};
	int ret;

	if (!device_is_ready(i2s_dev)) {
		LOG_ERR("%s is not ready", i2s_dev->name);
		return 0;
	}

	if (arm_rfft_fast_init_f32(&fft, FFT_SIZE) != ARM_MATH_SUCCESS) {
		LOG_ERR("CMSIS-DSP has no tables for a %u point transform", FFT_SIZE);
		return 0;
	}

	arm_hanning_f32(window, FFT_SIZE);
	bands_init();

	ret = display_start();
	if (ret < 0) {
		return 0;
	}

	ret = codec_start(&i2s_cfg);
	if (ret < 0) {
		return 0;
	}

	ret = i2s_configure(i2s_dev, I2S_DIR_RX, &i2s_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure %s for capture: %d", i2s_dev->name, ret);
		return 0;
	}

	ret = i2s_trigger(i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
	if (ret < 0) {
		LOG_ERR("Failed to start capture: %d", ret);
		return 0;
	}

	k_thread_create(&capture_tid, capture_stack, K_THREAD_STACK_SIZEOF(capture_stack),
			capture_thread, NULL, NULL, NULL, K_PRIO_COOP(7), 0, K_NO_WAIT);
	k_thread_name_set(&capture_tid, "capture");

	LOG_INF("%u bands of %u Hz audio, %u point transform", BANDS, SAMPLE_RATE, FFT_SIZE);

	for (;;) {
		k_sem_take(&frame_ready, K_FOREVER);
		analyze();
		render();
	}

	return 0;
}
