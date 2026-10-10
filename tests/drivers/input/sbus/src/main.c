/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/serial/uart_emul.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/input/input.h>
#include <zephyr/input/input_sbus.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define SBUS_FRAME_LEN       25
#define SBUS_HEADER          0x0F
#define SBUS_FOOTER          0x00
#define SBUS_CHANNEL_COUNT   16
#define SBUS_FLAGS_IDX       23
#define SBUS_FLAG_FRAME_LOST 0x04
#define SBUS_FLAG_FAILSAFE   0x08

/* Frame period of the test, below the 20 ms receiver timeout of the driver */
#define FRAME_PERIOD_MS  10
/* Time without frames after which the driver reports the receiver lost */
#define RECEIVER_LOST_MS 50

#define STICK_CENTER 992U
#define SWITCH_LOW   172U
#define SWITCH_HIGH  1811U

#define MAX_EVENTS 16

struct event_log {
	struct input_event events[MAX_EVENTS];
	size_t count;
	/* Receiver status read on the last sync event */
	struct input_sbus_status sync_status;
};

static const struct device *const uart_status = DEVICE_DT_GET(DT_NODELABEL(euart0));
static const struct device *const uart_plain = DEVICE_DT_GET(DT_NODELABEL(euart1));
static const struct device *const sbus_status = DEVICE_DT_GET(DT_NODELABEL(sbus_status));
static const struct device *const sbus_plain = DEVICE_DT_GET(DT_NODELABEL(sbus_plain));

static struct event_log log_status;
static struct event_log log_plain;

static void log_event(struct event_log *log, struct input_event *evt)
{
	if (evt->sync) {
		input_sbus_get_status(evt->dev, &log->sync_status);
	}

	/* Overflows show up as a wrong event count in the test */
	if (log->count < MAX_EVENTS) {
		log->events[log->count] = *evt;
	}
	log->count++;
}

static void sbus_status_cb(struct input_event *evt, void *user_data)
{
	log_event(&log_status, evt);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(sbus_status)), sbus_status_cb, NULL);

static void sbus_plain_cb(struct input_event *evt, void *user_data)
{
	log_event(&log_plain, evt);
}
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(DT_NODELABEL(sbus_plain)), sbus_plain_cb, NULL);

static void build_frame(uint8_t *frame, const uint16_t *channels, uint8_t flags)
{
	uint32_t bits = 0U;
	size_t nbits = 0U;
	size_t idx = 1U;

	memset(frame, 0, SBUS_FRAME_LEN);
	frame[0] = SBUS_HEADER;

	for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; i++) {
		bits |= (uint32_t)(channels[i] & 0x7FFU) << nbits;
		nbits += 11U;
		while (nbits >= 8U) {
			frame[idx] = bits & 0xFFU;
			idx++;
			bits >>= 8;
			nbits -= 8U;
		}
	}

	frame[SBUS_FLAGS_IDX] = flags;
	frame[SBUS_FRAME_LEN - 1] = SBUS_FOOTER;
}

/* Sends the same frame to both receivers and lets the drivers process it. */
static void send_frame(const uint16_t *channels, uint8_t flags)
{
	uint8_t frame[SBUS_FRAME_LEN];

	build_frame(frame, channels, flags);

	zassert_equal(uart_emul_put_rx_data(uart_status, frame, sizeof(frame)), sizeof(frame));
	zassert_equal(uart_emul_put_rx_data(uart_plain, frame, sizeof(frame)), sizeof(frame));

	k_sleep(K_MSEC(FRAME_PERIOD_MS));
}

static void default_channels(uint16_t *channels)
{
	for (size_t i = 0U; i < SBUS_CHANNEL_COUNT; i++) {
		channels[i] = STICK_CENTER;
	}
	channels[4] = SWITCH_LOW;
}

static void assert_event(const struct event_log *log, size_t idx, uint8_t type, uint16_t code,
			 int32_t value, bool sync)
{
	const struct input_event *evt;

	zassert_true(idx < log->count, "missing event %zu", idx);
	evt = &log->events[idx];

	zassert_equal(evt->type, type, "event %zu: type %u", idx, evt->type);
	zassert_equal(evt->code, code, "event %zu: code %u", idx, evt->code);
	zassert_equal(evt->value, value, "event %zu: value %d", idx, evt->value);
	zassert_equal(evt->sync, sync, "event %zu: sync %d", idx, evt->sync);
}

static void assert_sync(const struct event_log *log, size_t idx)
{
	assert_event(log, idx, 0, 0, 0, true);
}

static void reset_logs(void)
{
	memset(&log_status, 0, sizeof(log_status));
	memset(&log_plain, 0, sizeof(log_plain));
}

static void sbus_before(void *fixture)
{
	uint16_t channels[SBUS_CHANNEL_COUNT];

	ARG_UNUSED(fixture);

	/* Start each test with the receiver connected and all flags cleared */
	default_channels(channels);
	send_frame(channels, 0U);
	send_frame(channels, 0U);

	reset_logs();
}

ZTEST(input_sbus, test_init)
{
	struct uart_config cfg;

	zassert_true(device_is_ready(sbus_status));
	zassert_true(device_is_ready(sbus_plain));

	zassert_ok(uart_config_get(uart_status, &cfg));
	zassert_equal(cfg.baudrate, 100000);
	zassert_equal(cfg.parity, UART_CFG_PARITY_EVEN);
	zassert_equal(cfg.stop_bits, UART_CFG_STOP_BITS_2);
	zassert_equal(cfg.data_bits, UART_CFG_DATA_BITS_8);
}

ZTEST(input_sbus, test_channels)
{
	uint16_t channels[SBUS_CHANNEL_COUNT];
	const struct event_log *logs[] = {&log_status, &log_plain};

	default_channels(channels);
	channels[0] = 1000U;
	channels[1] = 1500U;
	channels[4] = SWITCH_HIGH;
	send_frame(channels, 0U);

	ARRAY_FOR_EACH(logs, i) {
		zassert_equal(logs[i]->count, 4);
		assert_event(logs[i], 0, INPUT_EV_ABS, INPUT_ABS_X, 1000, false);
		assert_event(logs[i], 1, INPUT_EV_ABS, INPUT_ABS_Y, 1500, false);
		assert_event(logs[i], 2, INPUT_EV_KEY, INPUT_KEY_0, 1, false);
		assert_sync(logs[i], 3);
	}

	/* Unchanged values are filtered, only the sync event is reported */
	reset_logs();
	send_frame(channels, 0U);

	ARRAY_FOR_EACH(logs, i) {
		zassert_equal(logs[i]->count, 1);
		assert_sync(logs[i], 0);
	}
}

ZTEST(input_sbus, test_failsafe)
{
	uint16_t channels[SBUS_CHANNEL_COUNT];

	/* The failsafe event comes before the channel values of the frame */
	default_channels(channels);
	channels[0] = 1200U;
	send_frame(channels, SBUS_FLAG_FAILSAFE);

	zassert_equal(log_status.count, 3);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F1, 1, false);
	assert_event(&log_status, 1, INPUT_EV_ABS, INPUT_ABS_X, 1200, false);
	assert_sync(&log_status, 2);

	zassert_equal(log_plain.count, 2);
	assert_event(&log_plain, 0, INPUT_EV_ABS, INPUT_ABS_X, 1200, false);
	assert_sync(&log_plain, 1);

	/* The flag is reported on change only */
	reset_logs();
	send_frame(channels, SBUS_FLAG_FAILSAFE);

	zassert_equal(log_status.count, 1);
	assert_sync(&log_status, 0);

	reset_logs();
	send_frame(channels, 0U);

	zassert_equal(log_status.count, 2);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F1, 0, false);
	assert_sync(&log_status, 1);

	zassert_equal(log_plain.count, 1);
	assert_sync(&log_plain, 0);
}

ZTEST(input_sbus, test_frame_lost)
{
	uint16_t channels[SBUS_CHANNEL_COUNT];

	default_channels(channels);
	send_frame(channels, SBUS_FLAG_FRAME_LOST);

	zassert_equal(log_status.count, 2);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F2, 1, false);
	assert_sync(&log_status, 1);

	zassert_equal(log_plain.count, 1);
	assert_sync(&log_plain, 0);

	/* Failsafe frames from most receivers carry both flags */
	reset_logs();
	send_frame(channels, SBUS_FLAG_FRAME_LOST | SBUS_FLAG_FAILSAFE);

	zassert_equal(log_status.count, 2);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F1, 1, false);
	assert_sync(&log_status, 1);

	reset_logs();
	send_frame(channels, 0U);

	zassert_equal(log_status.count, 3);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F1, 0, false);
	assert_event(&log_status, 1, INPUT_EV_KEY, INPUT_KEY_F2, 0, false);
	assert_sync(&log_status, 2);

	zassert_equal(log_plain.count, 1);
	assert_sync(&log_plain, 0);
}

ZTEST(input_sbus, test_receiver_lost)
{
	uint16_t channels[SBUS_CHANNEL_COUNT];

	/* No frame for longer than the receiver timeout */
	k_sleep(K_MSEC(RECEIVER_LOST_MS));

	zassert_equal(log_status.count, 1);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F3, 1, true);

	zassert_equal(log_plain.count, 0);

	/* Reported once, not on every timeout */
	k_sleep(K_MSEC(RECEIVER_LOST_MS));
	zassert_equal(log_status.count, 1);

	/* The next valid frame clears it, before the frame is reported */
	reset_logs();
	default_channels(channels);
	channels[1] = 1700U;
	send_frame(channels, 0U);

	zassert_equal(log_status.count, 3);
	assert_event(&log_status, 0, INPUT_EV_KEY, INPUT_KEY_F3, 0, false);
	assert_event(&log_status, 1, INPUT_EV_ABS, INPUT_ABS_Y, 1700, false);
	assert_sync(&log_status, 2);

	zassert_equal(log_plain.count, 2);
	assert_event(&log_plain, 0, INPUT_EV_ABS, INPUT_ABS_Y, 1700, false);
	assert_sync(&log_plain, 1);
}

ZTEST(input_sbus, test_status)
{
	const struct device *const devs[] = {sbus_status, sbus_plain};
	const struct event_log *logs[] = {&log_status, &log_plain};
	struct input_sbus_status before[ARRAY_SIZE(devs)];
	struct input_sbus_status status;
	uint16_t channels[SBUS_CHANNEL_COUNT];

	/* The status does not depend on the event code properties */
	ARRAY_FOR_EACH(devs, i) {
		input_sbus_get_status(devs[i], &before[i]);
		zassert_false(before[i].failsafe);
		zassert_false(before[i].frame_lost);
		zassert_false(before[i].receiver_lost);
		zassert_true(before[i].last_frame_ms > 0);
		zassert_true(before[i].last_frame_ms <= k_uptime_get());
	}

	default_channels(channels);
	send_frame(channels, SBUS_FLAG_FRAME_LOST | SBUS_FLAG_FAILSAFE);

	ARRAY_FOR_EACH(devs, i) {
		input_sbus_get_status(devs[i], &status);
		zassert_true(status.failsafe);
		zassert_true(status.frame_lost);
		zassert_false(status.receiver_lost);
		zassert_equal(status.frames_lost, before[i].frames_lost + 1U);
		zassert_true(status.last_frame_ms > before[i].last_frame_ms);

		/* Already up to date when the sync event of the frame is reported */
		zassert_true(logs[i]->sync_status.failsafe);
		zassert_true(logs[i]->sync_status.frame_lost);
	}

	k_sleep(K_MSEC(RECEIVER_LOST_MS));

	ARRAY_FOR_EACH(devs, i) {
		input_sbus_get_status(devs[i], &status);
		zassert_true(status.receiver_lost);
		zassert_true(k_uptime_get() - status.last_frame_ms >= RECEIVER_LOST_MS);
	}

	send_frame(channels, 0U);

	ARRAY_FOR_EACH(devs, i) {
		input_sbus_get_status(devs[i], &status);
		zassert_false(status.failsafe);
		zassert_false(status.frame_lost);
		zassert_false(status.receiver_lost);
		zassert_equal(status.frames_lost, before[i].frames_lost + 1U);

		zassert_false(logs[i]->sync_status.failsafe);
		zassert_false(logs[i]->sync_status.frame_lost);
	}
}

ZTEST_SUITE(input_sbus, NULL, NULL, sbus_before, NULL, NULL);
