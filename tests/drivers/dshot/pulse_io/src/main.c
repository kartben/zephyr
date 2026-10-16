/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dshot.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>
#include <zephyr/ztest.h>

#include "fake_pulse_io.h"

#define ESC_CHANNELS 4U

static const struct device *const esc = DEVICE_DT_GET(DT_NODELABEL(esc));
static const struct device *const esc_conflict = DEVICE_DT_GET(DT_NODELABEL(esc_conflict));
static const struct device *const esc_slow = DEVICE_DT_GET(DT_NODELABEL(esc_slow));
static const struct device *const pio0 = DEVICE_DT_GET(DT_NODELABEL(fake_pio0));
static const struct device *const pio1 = DEVICE_DT_GET(DT_NODELABEL(fake_pio1));

/* DShot channels 0-2 are on pio0 channels 0-2, channel 3 on pio1 channel 0 */
static struct fake_pio_channel *esc_ch(uint8_t channel)
{
	return (channel < 3U) ? fake_pio_channel(pio0, channel) : fake_pio_channel(pio1, 0);
}

static void clear_records(void)
{
	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		struct fake_pio_channel *ch = esc_ch(i);

		ch->configure_count = 0;
		ch->tx_count = 0;
		ch->last_count = 0;
		ch->history_len = 0;
	}
}

static int configure(enum dshot_speed speed)
{
	struct dshot_config cfg = {.speed = speed};

	return dshot_configure(esc, &cfg);
}

/* Apply @p speed even when it is the current one */
static void force_speed(enum dshot_speed speed)
{
	zassert_ok(configure((speed == DSHOT_SPEED_300) ? DSHOT_SPEED_600 : DSHOT_SPEED_300));
	zassert_ok(configure(speed));
}

static void assert_history(uint8_t channel, size_t count, uint16_t frame)
{
	struct fake_pio_channel *ch = esc_ch(channel);

	zassert_equal(ch->history_len, count, "channel %u sent %zu frames", channel,
		      ch->history_len);
	for (size_t i = 0; i < count; i++) {
		zassert_equal(ch->history[i], frame, "channel %u frame %zu: 0x%04x", channel, i,
			      ch->history[i]);
	}
}

ZTEST(dshot_pulse_io, test_ready_and_ownership)
{
	struct pulse_io_channel *handle;

	zassert_true(device_is_ready(esc));
	zassert_true(device_is_ready(esc_slow));
	zassert_equal(dshot_get_channel_count(esc), ESC_CHANNELS);
	zassert_equal(dshot_get_channel_count(esc_slow), 1U);

	/* The second device claiming a reserved channel fails to initialize */
	zassert_false(device_is_ready(esc_conflict));

	/* Reserved channels stay unavailable to other Pulse IO clients */
	zassert_equal(pulse_io_channel_get(pio0, 1, &handle), -EBUSY);
	zassert_equal(pulse_io_channel_get(pio1, 0, &handle), -EBUSY);
	zassert_equal(pulse_io_channel_get(pio0, 5, &handle), -EBUSY);

	zassert_ok(pulse_io_channel_get(pio0, 7, &handle));
	zassert_ok(pulse_io_channel_release(pio0, handle));
}

ZTEST(dshot_pulse_io, test_cell_configuration)
{
	struct fake_pio_channel *ch = esc_ch(0);

	zassert_equal(ch->cfg.mode, PULSE_IO_MODE_CELL);
	zassert_equal(ch->cfg.dir, PULSE_IO_DIR_TX);
	zassert_false(ch->cfg.idle_high);
	zassert_false(ch->cfg.carrier_en);
	/* The finest tick on offer is requested and rounded to 80 MHz */
	zassert_equal(ch->cfg.resolution_hz, NSEC_PER_SEC / 13U);
	zassert_equal(ch->resolution_hz, 80000000U);
	zassert_equal(ch->cfg.cell_period_ticks, 133U);

	/* The period estimated from the requested rate is corrected */
	clear_records();
	zassert_ok(configure(DSHOT_SPEED_300));
	zassert_equal(ch->configure_count, 2U);
	zassert_equal(ch->cfg.cell_period_ticks, 267U);

	/* The slow device was configured from its devicetree speed */
	zassert_equal(fake_pio_channel(pio0, 5)->cfg.cell_period_ticks, 533U);
}

ZTEST(dshot_pulse_io, test_send_frames)
{
	struct fake_pio_channel *ch = esc_ch(1);

	zassert_ok(dshot_set_throttle(esc, 0, DSHOT_THROTTLE_MIN, false));
	zassert_ok(dshot_set_throttle(esc, 1, 1046, true));
	zassert_ok(dshot_set_throttle(esc, 2, DSHOT_THROTTLE_MAX, false));
	zassert_ok(dshot_send(esc));

	assert_history(0, 1, 0x0606);
	assert_history(1, 1, 0x82d7);
	assert_history(2, 1, 0xffee);
	assert_history(3, 1, 0x0000);

	/* 16 data cells at 3/8 and 3/4 of 133 ticks, then two idle cells */
	zassert_equal(ch->last_count, 18U);
	for (size_t i = 0; i < 16U; i++) {
		bool one = (0x82d7 & BIT(15U - i)) != 0U;

		zassert_equal(ch->last.cells[i].duty, one ? 100U : 50U, "cell %zu", i);
	}
	zassert_equal(ch->last.cells[16].duty, 0U);
	zassert_equal(ch->last.cells[17].duty, 0U);

	/* Transmissions are bounded in time */
	zassert_false(K_TIMEOUT_EQ(ch->last_timeout, K_FOREVER));
	zassert_false(K_TIMEOUT_EQ(ch->last_timeout, K_NO_WAIT));
}

ZTEST(dshot_pulse_io, test_group_transmit)
{
	struct fake_pio_groups *groups = fake_pio_groups(pio0);

	zassert_ok(dshot_set_throttle(esc, 1, 1046, false));
	zassert_ok(dshot_send(esc));

	/* The three channels of pio0 start together on the same edge */
	zassert_equal(groups->calls, 1U);
	zassert_equal(groups->last_count, 3U);
	zassert_equal(groups->last_flags, PULSE_IO_TX_GROUP_SYNC);
	assert_history(1, 1, 0x82c6);

	/* pio1 has no group support and transmits on its own */
	zassert_equal(fake_pio_groups(pio1)->calls, 0U);
	assert_history(3, 1, 0x0000);
}

ZTEST(dshot_pulse_io, test_group_without_sync)
{
	struct fake_pio_groups *groups = fake_pio_groups(pio0);

	/* A controller that cannot align the starts still gets one group */
	fake_pio_caps(pio0)->tx_sync_multi_channel = false;
	force_speed(DSHOT_SPEED_600);
	zassert_ok(dshot_send(esc));
	zassert_equal(groups->calls, 1U);
	zassert_equal(groups->last_count, 3U);
	zassert_equal(groups->last_flags, 0U);
}

ZTEST(dshot_pulse_io, test_staged_value_persists)
{
	uint16_t frame = dshot_frame_encode(500, false);

	zassert_ok(dshot_set_throttle(esc, 0, 500, false));
	for (int i = 0; i < 3; i++) {
		zassert_ok(dshot_send(esc));
	}
	assert_history(0, 3, frame);
	assert_history(1, 3, 0x0000);
}

ZTEST(dshot_pulse_io, test_rejects_commands_as_throttle)
{
	for (uint16_t v = 1; v < DSHOT_THROTTLE_MIN; v++) {
		zassert_equal(dshot_set_throttle(esc, 0, v, false), -EINVAL, "value %u", v);
	}
	zassert_equal(dshot_set_throttle(esc, 0, DSHOT_THROTTLE_MAX + 1U, false), -EINVAL);
	zassert_equal(dshot_set_throttle(esc, ESC_CHANNELS, 100, false), -EINVAL);

	/* Nothing was sent and the staged stop value is unchanged */
	zassert_equal(esc_ch(0)->tx_count, 0U);
	zassert_ok(dshot_send(esc));
	assert_history(0, 1, 0x0000);
}

ZTEST(dshot_pulse_io, test_stop)
{
	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		zassert_ok(dshot_set_throttle(esc, i, 1000 + i, true));
	}
	zassert_ok(dshot_stop(esc));
	zassert_ok(dshot_send(esc));

	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		assert_history(i, 2, 0x0000);
	}
}

ZTEST(dshot_pulse_io, test_all_speeds)
{
	static const struct {
		enum dshot_speed speed;
		uint32_t bit_ticks;
		uint16_t t0h;
		uint16_t t1h;
		size_t cells;
	} expect[] = {
		{DSHOT_SPEED_150, 533, 200, 400, 18},
		{DSHOT_SPEED_300, 267, 100, 200, 18},
		{DSHOT_SPEED_600, 133, 50, 100, 18},
		{DSHOT_SPEED_1200, 67, 25, 50, 19},
	};
	struct fake_pio_channel *ch = esc_ch(0);

	for (size_t i = 0; i < ARRAY_SIZE(expect); i++) {
		zassert_ok(configure(expect[i].speed));
		zassert_equal(ch->cfg.cell_period_ticks, expect[i].bit_ticks, "DShot%u",
			      expect[i].speed);
		zassert_ok(dshot_set_throttle(esc, 0, 1046, false));
		zassert_ok(dshot_send(esc));
		/* The speed only changes while the motors are stopped */
		zassert_ok(dshot_set_throttle(esc, 0, DSHOT_THROTTLE_STOP, false));
		zassert_equal(ch->last_count, expect[i].cells, "DShot%u", expect[i].speed);
		zassert_equal(ch->last.cells[0].duty, expect[i].t1h, "DShot%u", expect[i].speed);
		zassert_equal(ch->last.cells[1].duty, expect[i].t0h, "DShot%u", expect[i].speed);
		zassert_equal(ch->history[ch->history_len - 1U], 0x82c6);
	}
}

ZTEST(dshot_pulse_io, test_invalid_speed)
{
	zassert_equal(configure((enum dshot_speed)400), -EINVAL);
	zassert_equal(configure((enum dshot_speed)0), -EINVAL);
	zassert_equal(esc_ch(0)->configure_count, 0U);
}

ZTEST(dshot_pulse_io, test_symbol_fallback)
{
	struct fake_pio_channel *ch = esc_ch(2);

	/* Level-only cells cannot carry a duty cycle */
	fake_pio_caps(pio0)->cell_duty_bits = 0;
	fake_pio_caps(pio1)->cell_duty_bits = 0;
	zassert_ok(configure(DSHOT_SPEED_300));
	zassert_equal(ch->cfg.mode, PULSE_IO_MODE_SYMBOL);

	zassert_ok(dshot_set_throttle(esc, 2, 1046, false));
	zassert_ok(dshot_send(esc));
	assert_history(2, 1, 0x82c6);

	zassert_equal(ch->last_count, 34U);
	for (size_t i = 0; i < 16U; i++) {
		bool one = (0x82c6 & BIT(15U - i)) != 0U;

		zassert_equal(ch->last.symbols[2U * i].level, 1U);
		zassert_equal(ch->last.symbols[2U * i].duration, one ? 200U : 100U);
		zassert_equal(ch->last.symbols[2U * i + 1U].level, 0U);
		zassert_equal(ch->last.symbols[2U * i].duration +
				      ch->last.symbols[2U * i + 1U].duration,
			      267U);
	}
	zassert_equal(ch->last.symbols[32].level, 0U);
	zassert_equal(ch->last.symbols[33].duration, 267U);
}

ZTEST(dshot_pulse_io, test_level_only_cells_unsupported)
{
	/* Only level cells: DShot cannot be generated at all */
	fake_pio_caps(pio0)->modes = PULSE_IO_MODE_CELL;
	fake_pio_caps(pio0)->cell_duty_bits = 0;
	zassert_equal(configure(DSHOT_SPEED_300), -ENOTSUP);

	/* Restoring the previous speed failed too, so nothing is sent */
	zassert_equal(dshot_send(esc), -EIO);
	zassert_equal(esc_ch(3)->tx_count, 0U);

	fake_pio_reset(pio0);
	zassert_ok(configure(DSHOT_SPEED_600));
	zassert_ok(dshot_send(esc));
}

ZTEST(dshot_pulse_io, test_unsupported_speed_keeps_previous)
{
	/* 10 MHz ticks: DShot300 is 1 % fast, DShot1200 would be 4 % fast */
	fake_pio_caps(pio0)->min_tick_ns = 100;
	fake_pio_caps(pio1)->min_tick_ns = 100;
	zassert_ok(configure(DSHOT_SPEED_300));
	zassert_equal(esc_ch(0)->cfg.cell_period_ticks, 33U);

	zassert_equal(configure(DSHOT_SPEED_1200), -ENOTSUP);
	zassert_equal(esc_ch(0)->cfg.cell_period_ticks, 33U);
	zassert_equal(esc_ch(3)->cfg.cell_period_ticks, 33U);

	zassert_ok(dshot_send(esc));
	zassert_equal(esc_ch(0)->last.cells[0].duty, 12U);
}

ZTEST(dshot_pulse_io, test_resolution_rounding)
{
	struct fake_pio_channel *ch = esc_ch(0);

	/* 48 MHz source: the 47.6 MHz request rounds to 48 MHz */
	fake_pio_set_src_clock(pio0, 48000000);
	fake_pio_caps(pio0)->min_tick_ns = 21;
	force_speed(DSHOT_SPEED_600);
	zassert_equal(ch->cfg.resolution_hz, NSEC_PER_SEC / 21U);
	zassert_equal(ch->cfg.cell_period_ticks, 80U);

	zassert_ok(dshot_set_throttle(esc, 0, 2047, false));
	zassert_ok(dshot_send(esc));
	zassert_equal(ch->last.cells[0].duty, 60U);
	zassert_equal(ch->last.cells[11].duty, 30U);
}

ZTEST(dshot_pulse_io, test_no_resolution_readback)
{
	struct fake_pio_channel *ch = esc_ch(0);

	/* Without readback the requested rate is assumed */
	fake_pio_set_readback(pio0, false);
	fake_pio_set_src_clock(pio0, NSEC_PER_SEC / 13U);
	force_speed(DSHOT_SPEED_600);
	zassert_equal(ch->cfg.cell_period_ticks, 128U);

	zassert_ok(dshot_send(esc));
	zassert_equal(ch->last.cells[0].duty, 48U);
}

ZTEST(dshot_pulse_io, test_transmit_error)
{
	esc_ch(1)->tx_error = -EIO;
	zassert_equal(dshot_send(esc), -EIO);

	/* The other channels are still sent */
	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		zassert_equal(esc_ch(i)->tx_count, 1U, "channel %u", i);
	}

	/* The first error wins */
	esc_ch(3)->tx_error = -ETIMEDOUT;
	zassert_equal(dshot_send(esc), -EIO);
	esc_ch(1)->tx_error = 0;
	zassert_equal(dshot_send(esc), -ETIMEDOUT);
	esc_ch(3)->tx_error = 0;
	zassert_ok(dshot_send(esc));
}

ZTEST(dshot_pulse_io, test_command_sequence)
{
	int64_t start = k_uptime_get();

	zassert_ok(dshot_send_command(esc, BIT(1), DSHOT_CMD_SPIN_DIRECTION_REVERSED));

	/* Ten identical frames with the telemetry bit, nine 1 ms gaps, 1 ms after */
	assert_history(1, 10, dshot_frame_encode(DSHOT_CMD_SPIN_DIRECTION_REVERSED, true));
	assert_history(0, 10, 0x0000);
	assert_history(2, 10, 0x0000);
	assert_history(3, 10, 0x0000);
	zassert_true(k_uptime_get() - start >= 10);

	/* The staged values are untouched */
	zassert_ok(dshot_send(esc));
	zassert_equal(esc_ch(1)->history[10], 0x0000);
}

ZTEST(dshot_pulse_io, test_command_beacon)
{
	int64_t start = k_uptime_get();

	zassert_ok(dshot_send_command(esc, BIT_MASK(ESC_CHANNELS), DSHOT_CMD_BEACON1));
	zassert_true(k_uptime_get() - start >= 260);
	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		assert_history(i, 1, 0x0033);
	}
}

ZTEST(dshot_pulse_io, test_command_requires_stopped_motors)
{
	zassert_ok(dshot_set_throttle(esc, 2, 100, false));
	zassert_equal(dshot_send_command(esc, BIT(0), DSHOT_CMD_BEACON1), -EBUSY);
	zassert_equal(esc_ch(0)->tx_count, 0U);

	/* A stopped motor with the telemetry bit is still stopped */
	zassert_ok(dshot_set_throttle(esc, 2, DSHOT_THROTTLE_STOP, true));
	zassert_ok(dshot_send_command(esc, BIT(0), DSHOT_CMD_LED0_ON));
}

ZTEST(dshot_pulse_io, test_command_invalid)
{
	zassert_equal(dshot_send_command(esc, 0, DSHOT_CMD_BEACON1), -EINVAL);
	zassert_equal(dshot_send_command(esc, BIT(ESC_CHANNELS), DSHOT_CMD_BEACON1), -EINVAL);
	zassert_equal(dshot_send_command(esc, BIT(0), (enum dshot_command)48), -EINVAL);
	zassert_equal(esc_ch(0)->tx_count, 0U);
}

ZTEST(dshot_pulse_io, test_configure_while_spinning)
{
	zassert_ok(dshot_set_throttle(esc, 3, 100, false));
	zassert_equal(configure(DSHOT_SPEED_300), -EBUSY);
	zassert_equal(esc_ch(0)->configure_count, 0U);
}

static K_THREAD_STACK_DEFINE(worker_stack, 2048);
static struct k_thread worker;
static volatile int worker_result;

static void command_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1);
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	worker_result = dshot_send_command(esc, BIT(0), DSHOT_CMD_SAVE_SETTINGS);
}

ZTEST(dshot_pulse_io, test_throttle_rejected_during_command)
{
	k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack),
			command_worker, NULL, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);

	/* The command takes 10 frames 1 ms apart, then 35 ms */
	k_msleep(20);
	zassert_true(esc_ch(0)->tx_count > 0U);
	zassert_equal(dshot_set_throttle(esc, 1, 100, false), -EBUSY);
	zassert_ok(dshot_set_throttle(esc, 1, DSHOT_THROTTLE_STOP, true));

	zassert_ok(k_thread_join(&worker, K_SECONDS(1)));
	zassert_ok(worker_result);
	assert_history(0, 10, dshot_frame_encode(DSHOT_CMD_SAVE_SETTINGS, true));

	zassert_ok(dshot_set_throttle(esc, 1, 100, false));
}

static void send_worker(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p2);
	ARG_UNUSED(p3);

	*(volatile int *)p1 = dshot_send(esc);
}

ZTEST(dshot_pulse_io, test_overlapping_sends)
{
	static K_THREAD_STACK_DEFINE(second_stack, 2048);
	static volatile int second_result;
	static struct k_thread second;
	struct fake_pio_channel *ch = esc_ch(0);
	struct k_sem gate;

	k_sem_init(&gate, 0, 2);
	ch->tx_gate = &gate;
	zassert_ok(dshot_set_throttle(esc, 0, 500, false));

	/* The first send blocks inside the channel 0 transmission */
	k_thread_create(&worker, worker_stack, K_THREAD_STACK_SIZEOF(worker_stack), send_worker,
			(void *)&worker_result, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_msleep(5);
	zassert_true(ch->tx_waiting);

	/* Staging does not wait for the transmission in flight */
	zassert_ok(dshot_set_throttle(esc, 0, 1046, false));

	/* A second send waits for the first one */
	k_thread_create(&second, second_stack, K_THREAD_STACK_SIZEOF(second_stack), send_worker,
			(void *)&second_result, NULL, NULL, K_PRIO_PREEMPT(0), 0, K_NO_WAIT);
	k_msleep(5);
	zassert_equal(esc_ch(1)->tx_count, 0U);

	k_sem_give(&gate);
	k_sem_give(&gate);
	zassert_ok(k_thread_join(&worker, K_SECONDS(1)));
	zassert_ok(k_thread_join(&second, K_SECONDS(1)));
	zassert_ok(worker_result);
	zassert_ok(second_result);

	/* The frame in flight kept the value staged when it started */
	zassert_equal(ch->history_len, 2U);
	zassert_equal(ch->history[0], dshot_frame_encode(500, false));
	zassert_equal(ch->history[1], dshot_frame_encode(1046, false));
	assert_history(1, 2, 0x0000);
}

static int isr_set_result;
static int isr_send_result;

static void isr_timer_fn(struct k_timer *timer)
{
	ARG_UNUSED(timer);

	isr_set_result = dshot_set_throttle(esc, 0, 700, false);
	isr_send_result = dshot_send(esc);
}

ZTEST(dshot_pulse_io, test_isr_context)
{
	struct k_timer timer;

	k_timer_init(&timer, isr_timer_fn, NULL);
	k_timer_start(&timer, K_MSEC(1), K_NO_WAIT);
	k_msleep(10);

	/* Staging works from an interrupt, transmitting does not */
	zassert_ok(isr_set_result);
	zassert_equal(isr_send_result, -EWOULDBLOCK);
	zassert_equal(esc_ch(0)->tx_count, 0U);
	zassert_ok(dshot_send(esc));
	assert_history(0, 1, dshot_frame_encode(700, false));
}

/* Runs in user mode when CONFIG_USERSPACE=y, to cover the syscall handlers */
ZTEST_USER(dshot_pulse_io, test_syscalls)
{
	struct dshot_config cfg = {.speed = DSHOT_SPEED_300};

	zassert_equal(dshot_get_channel_count(esc), ESC_CHANNELS);
	zassert_ok(dshot_configure(esc, &cfg));
	zassert_ok(dshot_set_throttle(esc, 0, 1046, true));
	zassert_equal(dshot_set_throttle(esc, 0, 47, false), -EINVAL);
	zassert_ok(dshot_send(esc));
	zassert_ok(dshot_stop(esc));
	zassert_ok(dshot_send_command(esc, BIT(1), DSHOT_CMD_LED0_ON));
	zassert_equal(dshot_send_command(esc, BIT(1), (enum dshot_command)60), -EINVAL);
}

static void *dshot_pulse_io_setup(void)
{
	k_object_access_grant(esc, k_current_get());
	return NULL;
}

static void dshot_pulse_io_before(void *fixture)
{
	ARG_UNUSED(fixture);

	for (uint8_t i = 0; i < ESC_CHANNELS; i++) {
		zassert_ok(dshot_set_throttle(esc, i, DSHOT_THROTTLE_STOP, false));
	}
	fake_pio_reset(pio0);
	fake_pio_reset(pio1);
	force_speed(DSHOT_SPEED_600);
	clear_records();
}

ZTEST_SUITE(dshot_pulse_io, NULL, dshot_pulse_io_setup, dshot_pulse_io_before, NULL, NULL);
