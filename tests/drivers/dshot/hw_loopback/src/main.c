/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dshot.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#define ESC_NODE DT_NODELABEL(esc)

#define ESC_CHANNELS DT_PROP_LEN(ESC_NODE, pulse_ios)

/* Edges captured by the receiver may be one tick early or late each */
#define EDGE_TOL_TICKS 2U

/* Largest bit rate deviation the driver accepts */
#define RATE_TOL_PPM 20000

static const struct device *const esc = DEVICE_DT_GET(ESC_NODE);
static const struct device *const pio = DEVICE_DT_GET(DT_PHANDLE_BY_IDX(ESC_NODE, pulse_ios, 0));
static const uint8_t capture_index[] = DT_PROP(DT_PATH(zephyr_user), capture_channels);

BUILD_ASSERT(ARRAY_SIZE(capture_index) == ESC_CHANNELS);

static struct pulse_io_channel *capture_chan[ESC_CHANNELS];
static uint32_t capture_hz;
static struct pulse_symbol symbols[96];

struct send_job {
	uint16_t throttle[ESC_CHANNELS];
	bool telemetry;
	uint32_t frames;
	enum dshot_command command;
	bool use_command;
	int result;
	uint32_t send_cycles;
};

static K_THREAD_STACK_DEFINE(sender_stack, 2048);
static struct k_thread sender_thread;

static void sender(void *p1, void *p2, void *p3)
{
	struct send_job *job = p1;

	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	/* Leave the receiver time to arm */
	k_msleep(20);

	if (job->use_command) {
		job->result = dshot_send_command(esc, BIT(0), job->command);
		return;
	}

	for (uint8_t ch = 0; ch < ESC_CHANNELS; ch++) {
		job->result = dshot_set_throttle(esc, ch, job->throttle[ch], job->telemetry);
		if (job->result != 0) {
			return;
		}
	}
	for (uint32_t f = 0; (f < job->frames) && (job->result == 0); f++) {
		uint32_t start = k_cycle_get_32();

		job->result = dshot_send(esc);
		job->send_cycles = k_cycle_get_32() - start;
	}
}

/* Capture at the finest tick; a capture ends after idle_bits idle bit periods */
static void capture_configure(uint8_t ch, uint32_t idle_bits, uint32_t bitrate)
{
	struct pulse_io_caps caps;
	struct pulse_io_config cfg = {
		.mode = PULSE_IO_MODE_SYMBOL,
		.dir = PULSE_IO_DIR_RX,
	};

	zassert_ok(pulse_io_get_capabilities(pio, &caps));
	cfg.resolution_hz = NSEC_PER_SEC / caps.min_tick_ns;
	cfg.rx_idle_threshold_ticks =
		(uint32_t)MIN((uint64_t)idle_bits * cfg.resolution_hz / bitrate,
			      caps.rx_idle_threshold_max);
	zassert_ok(pulse_io_channel_configure(pio, capture_chan[ch], &cfg));
	zassert_ok(pulse_io_channel_get_resolution(pio, capture_chan[ch], &capture_hz));
}

/* Send in the background and capture what channel ch puts on its pin */
static size_t capture(uint8_t ch, struct send_job *job)
{
	struct pulse_io_rx_req req = {
		.symbols = symbols,
		.capacity = ARRAY_SIZE(symbols),
	};
	size_t count = 0;

	k_thread_create(&sender_thread, sender_stack, K_THREAD_STACK_SIZEOF(sender_stack), sender,
			job, NULL, NULL, K_PRIO_PREEMPT(1), 0, K_NO_WAIT);
	zassert_ok(pulse_io_receive_sync(pio, capture_chan[ch], &req, &count, K_SECONDS(2)));
	zassert_ok(k_thread_join(&sender_thread, K_SECONDS(2)));
	zassert_ok(job->result);

	return count;
}

static uint32_t ticks_to_ns(uint32_t ticks)
{
	return (uint32_t)(((uint64_t)ticks * NSEC_PER_SEC) / capture_hz);
}

/* A frame ends with a low level that merges into the idle time */
#define FRAME_SYMBOLS (2U * DSHOT_FRAME_BITS - 1U)

/*
 * Check one captured frame starting at syms[0] against the waveform the
 * driver derives from the same tick rate, and return its 16 bits.
 */
static uint16_t check_frame(const struct pulse_symbol *syms, uint32_t bitrate)
{
	uint32_t bit = DIV_ROUND_CLOSEST(capture_hz, bitrate);
	uint32_t t0h = DIV_ROUND_CLOSEST(bit * 3U, 8U);
	uint32_t t1h = DIV_ROUND_CLOSEST(bit * 3U, 4U);
	uint64_t total = 0;
	uint16_t frame = 0;
	int64_t error_ppm;

	for (uint32_t i = 0; i < DSHOT_FRAME_BITS; i++) {
		const struct pulse_symbol *high = &syms[2U * i];
		bool one = high->duration > bit / 2U;

		zassert_equal(high->level, 1U, "bit %u", i);
		zassert_within(high->duration, one ? t1h : t0h, EDGE_TOL_TICKS, "bit %u high %u", i,
			       high->duration);
		if (i < DSHOT_FRAME_BITS - 1U) {
			const struct pulse_symbol *low = &syms[2U * i + 1U];

			zassert_equal(low->level, 0U, "bit %u", i);
			zassert_within(high->duration + low->duration, bit, EDGE_TOL_TICKS,
				       "bit %u period %u", i, high->duration + low->duration);
			total += high->duration + low->duration;
		}
		frame = (uint16_t)((frame << 1) | (one ? 1U : 0U));
	}

	/* The last low merges into the idle time and is not a full period */
	error_ppm = ((int64_t)capture_hz * (DSHOT_FRAME_BITS - 1U) - (int64_t)bitrate * total) *
		    1000000 / ((int64_t)bitrate * total);
	zassert_true((error_ppm <= RATE_TOL_PPM) && (error_ppm >= -RATE_TOL_PPM),
		     "bit rate off by %lld ppm", error_ppm);

	TC_PRINT("%u bit/s: bit %u ns, high %u/%u ns, rate error %lld ppm\n", bitrate,
		 ticks_to_ns(bit), ticks_to_ns(t0h), ticks_to_ns(t1h), error_ppm);

	return frame;
}

static int set_speed(enum dshot_speed speed)
{
	struct dshot_config cfg = {.speed = speed};

	zassert_ok(dshot_stop(esc));
	return dshot_configure(esc, &cfg);
}

ZTEST(dshot_hw_loopback, test_frames_all_speeds)
{
	static const enum dshot_speed speeds[] = {
		DSHOT_SPEED_150, DSHOT_SPEED_300, DSHOT_SPEED_600, DSHOT_SPEED_1200,
	};
	struct send_job job = {
		.throttle = {1046, DSHOT_THROTTLE_MIN},
		.telemetry = true,
		.frames = 1,
	};

	for (size_t s = 0; s < ARRAY_SIZE(speeds); s++) {
		uint32_t bitrate = (uint32_t)speeds[s] * 1000U;
		int ret = set_speed(speeds[s]);

		/* Coarse ticks cannot reach every speed; DShot600 is required */
		if ((ret == -ENOTSUP) && (speeds[s] != DSHOT_SPEED_600)) {
			TC_PRINT("DShot%u not supported by this controller\n", speeds[s]);
			continue;
		}
		zassert_ok(ret, "DShot%u", speeds[s]);

		for (uint8_t ch = 0; ch < ESC_CHANNELS; ch++) {
			size_t count;

			capture_configure(ch, 4U, bitrate);
			count = capture(ch, &job);
			zassert_true(count >= FRAME_SYMBOLS, "DShot%u channel %u: %zu", speeds[s],
				     ch, count);
			zassert_equal(check_frame(symbols, bitrate),
				      dshot_frame_encode(job.throttle[ch], true),
				      "DShot%u channel %u", speeds[s], ch);
		}
	}
}

ZTEST(dshot_hw_loopback, test_stop_and_command_frames)
{
	struct send_job job = {.frames = 1};
	size_t count;

	zassert_ok(set_speed(DSHOT_SPEED_600));
	capture_configure(0, 4U, 600000U);

	count = capture(0, &job);
	zassert_true(count >= FRAME_SYMBOLS);
	zassert_equal(check_frame(symbols, 600000U), 0x0000);

	/* The capture ends after the first of the ten repetitions */
	job = (struct send_job){
		.use_command = true,
		.command = DSHOT_CMD_SPIN_DIRECTION_NORMAL,
	};
	count = capture(0, &job);
	zassert_true(count >= FRAME_SYMBOLS);
	zassert_equal(check_frame(symbols, 600000U),
		      dshot_frame_encode(DSHOT_CMD_SPIN_DIRECTION_NORMAL, true));
}

ZTEST(dshot_hw_loopback, test_frame_interval)
{
	struct send_job job = {
		.throttle = {1046, 1046},
		.frames = 2,
	};
	uint64_t interval = 0;
	uint32_t bit;
	size_t count;

	zassert_ok(set_speed(DSHOT_SPEED_600));

	/* An idle threshold longer than the time between two calls */
	capture_configure(0, 600U, 600000U);
	bit = DIV_ROUND_CLOSEST(capture_hz, 600000U);
	count = capture(0, &job);
	zassert_true(count >= 2U * DSHOT_FRAME_BITS + FRAME_SYMBOLS,
		     "both frames in one capture: %zu", count);

	for (size_t i = 0; i < 2U * DSHOT_FRAME_BITS; i++) {
		interval += symbols[i].duration;
	}
	zassert_equal(check_frame(&symbols[2U * DSHOT_FRAME_BITS], 600000U),
		      dshot_frame_encode(1046, false));

	/* The frame and its idle bits come before anything else */
	zassert_true(interval >= (DSHOT_FRAME_BITS + 2U) * bit);

	TC_PRINT("%u channels at DShot600: frame start to frame start %u us, "
		 "dshot_send() took %u us\n",
		 ESC_CHANNELS, ticks_to_ns((uint32_t)interval) / 1000U,
		 k_cyc_to_us_floor32(job.send_cycles));
}

static void *dshot_hw_loopback_setup(void)
{
	zassert_true(device_is_ready(esc));
	for (uint8_t ch = 0; ch < ESC_CHANNELS; ch++) {
		zassert_ok(pulse_io_channel_get(pio, capture_index[ch], &capture_chan[ch]));
	}

	return NULL;
}

ZTEST_SUITE(dshot_hw_loopback, NULL, dshot_hw_loopback_setup, NULL, NULL, NULL);
