/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/dshot.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

/* ESCs arm after receiving motor stop frames for a while */
#define ARM_TIME_MS 3000

#define REPORT_INTERVAL_MS 1000

#define FRAME_PERIOD_US (USEC_PER_SEC / CONFIG_SAMPLE_DSHOT_UPDATE_RATE_HZ)

BUILD_ASSERT((CONFIG_SAMPLE_DSHOT_THROTTLE == 0) ||
		     (CONFIG_SAMPLE_DSHOT_THROTTLE >= DSHOT_THROTTLE_MIN),
	     "values 1 to 47 are commands, not throttle values");

static const struct device *const esc = DEVICE_DT_GET(DT_ALIAS(dshot0));

static K_TIMER_DEFINE(frame_timer, NULL, NULL);

static struct {
	uint32_t frames;
	uint32_t errors;
	uint32_t max_cycles;
	uint64_t total_cycles;
} stats;

/* Throttle of the ramp at frame i of n: up to the test value, then down */
static uint16_t ramp_throttle(uint32_t i, uint32_t n)
{
	uint32_t span = CONFIG_SAMPLE_DSHOT_THROTTLE - DSHOT_THROTTLE_MIN;
	uint32_t pos = (i < n / 2U) ? i : (n - 1U - i);

	return (uint16_t)(DSHOT_THROTTLE_MIN + (span * pos) / MAX(n / 2U, 1U));
}

/* Send frames at the update rate for duration_ms, ramping when ramp is set */
static int run_frames(uint32_t duration_ms, bool ramp)
{
	uint32_t n = (duration_ms * CONFIG_SAMPLE_DSHOT_UPDATE_RATE_HZ) / MSEC_PER_SEC;
	uint8_t channels = dshot_get_channel_count(esc);

	for (uint32_t i = 0; i < n; i++) {
		uint32_t start;
		uint32_t cycles;
		int ret;

		k_timer_status_sync(&frame_timer);

		if (ramp) {
			for (uint8_t ch = 0; ch < channels; ch++) {
				ret = dshot_set_throttle(esc, ch, ramp_throttle(i, n), false);
				if (ret != 0) {
					return ret;
				}
			}
		}

		start = k_cycle_get_32();
		ret = dshot_send(esc);
		cycles = k_cycle_get_32() - start;

		stats.frames++;
		stats.total_cycles += cycles;
		stats.max_cycles = MAX(stats.max_cycles, cycles);
		if (ret != 0) {
			stats.errors++;
		}
	}

	return 0;
}

static void report(void)
{
	uint32_t avg = (stats.frames > 0U) ? (uint32_t)(stats.total_cycles / stats.frames) : 0U;

	printk("frames sent: %u, errors: %u, dshot_send() average %u us, max %u us\n",
	       stats.frames, stats.errors, k_cyc_to_us_floor32(avg),
	       k_cyc_to_us_floor32(stats.max_cycles));
}

int main(void)
{
	struct dshot_config cfg = {.speed = (enum dshot_speed)CONFIG_SAMPLE_DSHOT_SPEED};
	uint8_t channels;
	int ret;

	if (!device_is_ready(esc)) {
		printk("DShot device %s not ready\n", esc->name);
		return 0;
	}

	ret = dshot_configure(esc, &cfg);
	if (ret != 0) {
		printk("DShot%u is not available on this hardware (%d)\n", cfg.speed, ret);
		return 0;
	}

	channels = dshot_get_channel_count(esc);
	printk("DShot%u on %u channels\n", cfg.speed, channels);
	printk("motor stop frame 0x%04x\n", dshot_frame_encode(DSHOT_THROTTLE_STOP, false));

	k_timer_start(&frame_timer, K_USEC(FRAME_PERIOD_US), K_USEC(FRAME_PERIOD_US));

	printk("arming: sending motor stop frames for %u ms\n", ARM_TIME_MS);
	(void)run_frames(ARM_TIME_MS, false);
	printk("armed\n");
	report();

	if (IS_ENABLED(CONFIG_SAMPLE_DSHOT_BEACON)) {
		ret = dshot_send_command(esc, BIT_MASK(channels), DSHOT_CMD_BEACON1);
		printk("beacon command sent (%d), frame 0x%04x\n", ret,
		       dshot_frame_encode(DSHOT_CMD_BEACON1, true));
	}

	if (CONFIG_SAMPLE_DSHOT_THROTTLE > 0) {
		printk("ramping to throttle %u and back over %u ms, frame 0x%04x at the top\n",
		       CONFIG_SAMPLE_DSHOT_THROTTLE, CONFIG_SAMPLE_DSHOT_THROTTLE_MS,
		       dshot_frame_encode(CONFIG_SAMPLE_DSHOT_THROTTLE, false));
		ret = run_frames(CONFIG_SAMPLE_DSHOT_THROTTLE_MS, true);
		if (ret != 0) {
			printk("throttle %u rejected (%d)\n", CONFIG_SAMPLE_DSHOT_THROTTLE, ret);
		}
		ret = dshot_stop(esc);
		printk("motors stopped (%d)\n", ret);
	}

	/* Keep the ESCs armed and fed with motor stop frames */
	while (true) {
		(void)run_frames(REPORT_INTERVAL_MS, false);
		report();
	}

	return 0;
}
