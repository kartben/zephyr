/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/dshot.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/ztest.h>

#include "dshot_command.h"
#include "dshot_pulse_io_wave.h"

/*
 * Frames worked out by hand from the frame layout: 11-bit value, telemetry
 * bit, then the XOR of the three nibbles of the first 12 bits.
 */
static const struct {
	uint16_t value;
	bool telemetry;
	uint16_t frame;
} frame_vectors[] = {
	{0, false, 0x0000},    {0, true, 0x0011},     {1, true, 0x0033},
	{20, true, 0x029b},    {21, true, 0x02b9},    {47, true, 0x05fa},
	{48, false, 0x0606},   {1000, false, 0x7d0a}, {1046, false, 0x82c6},
	{1046, true, 0x82d7},  {2047, false, 0xffee}, {2047, true, 0xffff},
};

ZTEST(dshot_frame, test_known_vectors)
{
	for (size_t i = 0; i < ARRAY_SIZE(frame_vectors); i++) {
		uint16_t frame = dshot_frame_encode(frame_vectors[i].value,
						    frame_vectors[i].telemetry);

		zassert_equal(frame, frame_vectors[i].frame, "value %u telemetry %d: 0x%04x",
			      frame_vectors[i].value, frame_vectors[i].telemetry, frame);
	}
}

ZTEST(dshot_frame, test_checksum_all_payloads)
{
	/* The checksum makes the XOR of the four nibbles of every frame zero */
	for (uint16_t value = 0; value <= DSHOT_VALUE_MAX; value++) {
		for (int telemetry = 0; telemetry < 2; telemetry++) {
			uint16_t frame = dshot_frame_encode(value, telemetry != 0);
			uint16_t x = frame ^ (frame >> 4) ^ (frame >> 8) ^ (frame >> 12);

			zassert_equal(x & 0xFU, 0U, "value %u", value);
			zassert_equal(dshot_frame_value(frame), value);
			zassert_equal((frame >> 4) & 1U, (uint16_t)telemetry);
		}
	}
}

ZTEST(dshot_frame, test_telemetry_bit)
{
	uint16_t without = dshot_frame_encode(1046, false);
	uint16_t with = dshot_frame_encode(1046, true);

	/* Bit 4 carries the request; the checksum absorbs the change */
	zassert_equal((without ^ with) & 0xFFF0U, BIT(4));
	zassert_not_equal(without & 0xFU, with & 0xFU);
}

ZTEST(dshot_frame, test_value_is_masked)
{
	zassert_equal(dshot_frame_encode(DSHOT_VALUE_MAX + 1U, false),
		      dshot_frame_encode(0, false));
}

ZTEST(dshot_frame, test_throttle_boundaries)
{
	zassert_true(dshot_throttle_is_valid(DSHOT_THROTTLE_STOP));
	zassert_false(dshot_throttle_is_valid(1));
	zassert_false(dshot_throttle_is_valid(DSHOT_CMD_MAX));
	zassert_true(dshot_throttle_is_valid(DSHOT_THROTTLE_MIN));
	zassert_true(dshot_throttle_is_valid(DSHOT_THROTTLE_MAX));
	zassert_false(dshot_throttle_is_valid(DSHOT_THROTTLE_MAX + 1U));
	zassert_false(dshot_throttle_is_valid(UINT16_MAX));

	for (uint16_t v = 1; v < DSHOT_THROTTLE_MIN; v++) {
		zassert_false(dshot_throttle_is_valid(v), "command %u accepted", v);
	}
}

ZTEST_SUITE(dshot_frame, NULL, NULL, NULL, NULL, NULL);

ZTEST(dshot_command, test_telemetry_bit_on_commands)
{
	struct dshot_command_info info;

	dshot_command_get_info(DSHOT_CMD_MOTOR_STOP, &info);
	zassert_false(info.telemetry);

	for (uint32_t cmd = 1; cmd <= DSHOT_CMD_MAX; cmd++) {
		dshot_command_get_info((enum dshot_command)cmd, &info);
		zassert_true(info.telemetry, "command %u", cmd);
		zassert_true(info.repeat >= 1U, "command %u", cmd);
		zassert_true(info.delay_us >= DSHOT_COMMAND_INTERVAL_US, "command %u", cmd);
	}
}

ZTEST(dshot_command, test_repeat_and_delay)
{
	struct dshot_command_info info;

	dshot_command_get_info(DSHOT_CMD_BEACON3, &info);
	zassert_equal(info.repeat, 1U);
	zassert_equal(info.delay_us, 260000U);

	dshot_command_get_info(DSHOT_CMD_ESC_INFO, &info);
	zassert_equal(info.repeat, 6U);
	zassert_equal(info.delay_us, 12000U);

	dshot_command_get_info(DSHOT_CMD_SAVE_SETTINGS, &info);
	zassert_equal(info.repeat, 10U);
	zassert_equal(info.delay_us, 35000U);

	dshot_command_get_info(DSHOT_CMD_SPIN_DIRECTION_REVERSED, &info);
	zassert_equal(info.repeat, 10U);

	dshot_command_get_info(DSHOT_CMD_EXTENDED_TELEMETRY_ENABLE, &info);
	zassert_equal(info.repeat, 10U);

	dshot_command_get_info(DSHOT_CMD_LED0_ON, &info);
	zassert_equal(info.repeat, 6U);

	/* Toggles must not be repeated */
	dshot_command_get_info(DSHOT_CMD_SILENT_MODE_ON_OFF, &info);
	zassert_equal(info.repeat, 1U);
}

ZTEST_SUITE(dshot_command, NULL, NULL, NULL, NULL, NULL);

/* Capabilities of a backend like the ESP32-S3 RMT: 80 MHz, 15-bit fields */
static struct pulse_io_caps rmt_like_caps(void)
{
	return (struct pulse_io_caps){
		.modes = PULSE_IO_MODE_SYMBOL | PULSE_IO_MODE_CELL,
		.supports_tx = true,
		.min_tick_ns = 13,
		.max_tick_ns = 819200,
		.max_duration_ticks = 32767,
		.tx_channel_mask = BIT_MASK(4),
		.num_channels = 8,
		.cell_duty_bits = 15,
	};
}

ZTEST(dshot_pio_select, test_prefers_duty_cells)
{
	struct pulse_io_caps caps = rmt_like_caps();
	struct dshot_pio_request req;

	zassert_ok(dshot_pio_select(&caps, 0, 600000, &req));
	zassert_equal(req.mode, PULSE_IO_MODE_CELL);
	/* The finest tick is requested; the backend rounds it to 80 MHz */
	zassert_equal(req.resolution_hz, NSEC_PER_SEC / 13U);
	zassert_equal(req.max_ticks, 32767U);
}

ZTEST(dshot_pio_select, test_level_only_cells_use_symbols)
{
	struct pulse_io_caps caps = rmt_like_caps();
	struct dshot_pio_request req;

	caps.cell_duty_bits = 0;
	zassert_ok(dshot_pio_select(&caps, 0, 600000, &req));
	zassert_equal(req.mode, PULSE_IO_MODE_SYMBOL);

	/* Level-only cells alone cannot encode a DShot bit */
	caps.modes = PULSE_IO_MODE_CELL;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);
}

ZTEST(dshot_pio_select, test_narrow_duty_field)
{
	struct pulse_io_caps caps = rmt_like_caps();
	struct dshot_pio_request req;

	/* 4-bit duty: up to 15 ticks per bit, so the tick is capped */
	caps.cell_duty_bits = 4;
	zassert_ok(dshot_pio_select(&caps, 0, 600000, &req));
	zassert_equal(req.mode, PULSE_IO_MODE_CELL);
	zassert_equal(req.max_ticks, 15U);
	zassert_equal(req.resolution_hz, 600000U * 15U);

	/* 3-bit duty cannot reach 8 ticks per bit; symbols take over */
	caps.cell_duty_bits = 3;
	zassert_ok(dshot_pio_select(&caps, 0, 600000, &req));
	zassert_equal(req.mode, PULSE_IO_MODE_SYMBOL);

	caps.modes = PULSE_IO_MODE_CELL;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);
}

ZTEST(dshot_pio_select, test_invalid_capabilities)
{
	struct pulse_io_caps caps = rmt_like_caps();
	struct dshot_pio_request req;

	/* Receive-only channel */
	zassert_equal(dshot_pio_select(&caps, 5, 600000, &req), -ENOTSUP);
	zassert_equal(dshot_pio_select(&caps, 200, 600000, &req), -ENOTSUP);

	caps.supports_tx = false;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);

	caps = rmt_like_caps();
	caps.modes = 0;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);

	caps = rmt_like_caps();
	caps.min_tick_ns = 0;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);

	zassert_equal(dshot_pio_select(NULL, 0, 600000, &req), -EINVAL);
	zassert_equal(dshot_pio_select(&caps, 0, 0, &req), -EINVAL);
}

ZTEST(dshot_pio_select, test_unsupported_resolution)
{
	struct pulse_io_caps caps = rmt_like_caps();
	struct dshot_pio_request req;

	/* 1 MHz is below 8 ticks per bit for every DShot rate */
	caps.min_tick_ns = 1000;
	zassert_equal(dshot_pio_select(&caps, 0, 150000, &req), -ENOTSUP);

	/* 8 MHz gives 13.3 ticks per DShot600 bit but 6.7 per DShot1200 bit */
	caps.min_tick_ns = 125;
	zassert_ok(dshot_pio_select(&caps, 0, 600000, &req));
	zassert_equal(dshot_pio_select(&caps, 0, 1200000, &req), -ENOTSUP);

	/* A backend whose slowest tick is still too fast for 100-tick fields */
	caps = rmt_like_caps();
	caps.min_tick_ns = 1;
	caps.max_tick_ns = 10;
	caps.max_duration_ticks = 100;
	caps.cell_duty_bits = 0;
	zassert_equal(dshot_pio_select(&caps, 0, 600000, &req), -ENOTSUP);
}

ZTEST_SUITE(dshot_pio_select, NULL, NULL, NULL, NULL, NULL);

ZTEST(dshot_pio_timing, test_all_speeds_at_80mhz)
{
	static const struct {
		uint32_t bitrate;
		uint32_t bit_ticks;
		uint32_t t0h;
		uint32_t t1h;
		int32_t error_ppm;
		uint32_t gap_bits;
	} expect[] = {
		{150000, 533, 200, 400, 625, 2},
		{300000, 267, 100, 200, -1248, 2},
		{600000, 133, 50, 100, 2506, 2},
		{1200000, 67, 25, 50, -4975, 3},
	};
	struct dshot_pio_timing t;

	for (size_t i = 0; i < ARRAY_SIZE(expect); i++) {
		zassert_ok(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, expect[i].bitrate,
						 MHZ(80), 32767));
		zassert_equal(t.bit_ticks, expect[i].bit_ticks, "%u", expect[i].bitrate);
		zassert_equal(t.t0h_ticks, expect[i].t0h, "%u", expect[i].bitrate);
		zassert_equal(t.t1h_ticks, expect[i].t1h, "%u", expect[i].bitrate);
		zassert_equal(t.rate_error_ppm, expect[i].error_ppm, "%u", expect[i].bitrate);
		zassert_equal(t.gap_bits, expect[i].gap_bits, "%u", expect[i].bitrate);
		zassert_equal(t.resolution_hz, MHZ(80));
		zassert_equal(t.mode, PULSE_IO_MODE_CELL);
	}
}

ZTEST(dshot_pio_timing, test_exact_eight_ticks)
{
	struct dshot_pio_timing t;

	/* 8 ticks per bit give exactly 3/8 and 6/8 high */
	zassert_ok(dshot_pio_timing_init(&t, PULSE_IO_MODE_SYMBOL, 600000, 4800000, 32767));
	zassert_equal(t.bit_ticks, 8U);
	zassert_equal(t.t0h_ticks, 3U);
	zassert_equal(t.t1h_ticks, 6U);
	zassert_equal(t.rate_error_ppm, 0);
}

ZTEST(dshot_pio_timing, test_quantization_limits)
{
	struct dshot_pio_timing t;

	/* 7 ticks per bit cannot keep the two high times apart */
	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 600000, 4200000, 32767),
		      -ENOTSUP);

	/* 9.5 ticks round to 10: 570 kbit/s is 5 % slow */
	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 600000, 5700000, 32767),
		      -ENOTSUP);

	/* 16.67 ticks round to 17: 588 kbit/s is within 2 % */
	zassert_ok(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 600000, MHZ(10), 32767));
	zassert_equal(t.bit_ticks, 17U);
	zassert_equal(t.rate_error_ppm, -19607);

	/* 8.33 ticks round to 8: DShot1200 would run 4 % fast */
	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 1200000, MHZ(10), 32767),
		      -ENOTSUP);

	/* The bit period has to fit in the backend fields */
	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 150000, MHZ(80), 255),
		      -ENOTSUP);

	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 600000, 0, 32767), -EINVAL);
	zassert_equal(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 0, MHZ(80), 32767), -EINVAL);
}

ZTEST_SUITE(dshot_pio_timing, NULL, NULL, NULL, NULL, NULL);

static struct dshot_pio_timing timing_600k_80mhz(enum pulse_io_mode mode)
{
	struct dshot_pio_timing t;

	zassert_ok(dshot_pio_timing_init(&t, mode, 600000, MHZ(80), 32767));
	return t;
}

ZTEST(dshot_pio_encode, test_cells_msb_first)
{
	struct dshot_pio_timing t = timing_600k_80mhz(PULSE_IO_MODE_CELL);
	struct pulse_cell cells[DSHOT_PIO_MAX_CELLS];
	uint16_t frame = dshot_frame_encode(1046, false);
	size_t count = 0;

	zassert_equal(frame, 0x82c6);
	zassert_ok(dshot_pio_encode_cells(&t, frame, cells, ARRAY_SIZE(cells), &count));
	zassert_equal(count, DSHOT_FRAME_BITS + 2U);

	/* 1000001011000110, first bit on the wire first */
	static const uint8_t bits[] = {1, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 0, 0, 1, 1, 0};

	for (size_t i = 0; i < DSHOT_FRAME_BITS; i++) {
		zassert_equal(cells[i].duty, bits[i] ? 100U : 50U, "bit %zu", i);
		zassert_equal(cells[i].rsvd, 0U);
	}
	for (size_t i = DSHOT_FRAME_BITS; i < count; i++) {
		zassert_equal(cells[i].duty, 0U, "gap cell %zu", i);
	}
}

ZTEST(dshot_pio_encode, test_single_bit_frames)
{
	struct dshot_pio_timing t = timing_600k_80mhz(PULSE_IO_MODE_CELL);
	struct pulse_cell cells[DSHOT_PIO_MAX_CELLS];
	size_t count;

	for (uint32_t b = 0; b < DSHOT_FRAME_BITS; b++) {
		zassert_ok(dshot_pio_encode_cells(&t, (uint16_t)BIT(b), cells, ARRAY_SIZE(cells),
						  &count));
		for (uint32_t i = 0; i < DSHOT_FRAME_BITS; i++) {
			/* Frame bit b is cell 15 - b */
			uint32_t want = (i == DSHOT_FRAME_BITS - 1U - b) ? 100U : 50U;

			zassert_equal(cells[i].duty, want, "frame bit %u cell %u", b, i);
		}
	}
}

ZTEST(dshot_pio_encode, test_symbols)
{
	struct dshot_pio_timing t = timing_600k_80mhz(PULSE_IO_MODE_SYMBOL);
	struct pulse_symbol symbols[DSHOT_PIO_MAX_SYMBOLS];
	uint16_t frame = dshot_frame_encode(1046, false);
	size_t count = 0;

	zassert_ok(dshot_pio_encode_symbols(&t, frame, symbols, ARRAY_SIZE(symbols), &count));
	zassert_equal(count, 2U * DSHOT_FRAME_BITS + 2U);

	for (uint32_t i = 0; i < DSHOT_FRAME_BITS; i++) {
		bool one = (frame & BIT(DSHOT_FRAME_BITS - 1U - i)) != 0U;
		const struct pulse_symbol *high = &symbols[2U * i];
		const struct pulse_symbol *low = &symbols[2U * i + 1U];

		zassert_equal(high->level, 1U, "bit %u", i);
		zassert_equal(low->level, 0U, "bit %u", i);
		zassert_equal(high->duration, one ? 100U : 50U, "bit %u", i);
		zassert_equal(high->duration + low->duration, 133U, "bit %u", i);
	}
	for (size_t i = 2U * DSHOT_FRAME_BITS; i < count; i++) {
		zassert_equal(symbols[i].level, 0U);
		zassert_equal(symbols[i].duration, 133U);
	}
}

ZTEST(dshot_pio_encode, test_gap_dshot1200)
{
	struct pulse_symbol symbols[DSHOT_PIO_MAX_SYMBOLS];
	struct pulse_cell cells[DSHOT_PIO_MAX_CELLS];
	struct dshot_pio_timing t;
	size_t count;

	/* Two 0.833 us bit periods are under 2 us; three are needed */
	zassert_ok(dshot_pio_timing_init(&t, PULSE_IO_MODE_CELL, 1200000, MHZ(80), 32767));
	zassert_ok(dshot_pio_encode_cells(&t, 0, cells, ARRAY_SIZE(cells), &count));
	zassert_equal(count, DSHOT_FRAME_BITS + 3U);
	zassert_ok(dshot_pio_encode_symbols(&t, 0, symbols, ARRAY_SIZE(symbols), &count));
	zassert_equal(count, 2U * DSHOT_FRAME_BITS + 3U);
	zassert_true((uint64_t)t.gap_bits * t.bit_ticks * NSEC_PER_SEC / t.resolution_hz >= 2000U);
}

ZTEST(dshot_pio_encode, test_buffer_too_small)
{
	struct dshot_pio_timing t = timing_600k_80mhz(PULSE_IO_MODE_CELL);
	struct pulse_symbol symbols[DSHOT_PIO_MAX_SYMBOLS];
	struct pulse_cell cells[DSHOT_PIO_MAX_CELLS];
	size_t count = 0;

	zassert_equal(dshot_pio_encode_cells(&t, 0, cells, DSHOT_FRAME_BITS + 1U, &count),
		      -ENOMEM);
	zassert_equal(dshot_pio_encode_symbols(&t, 0, symbols, 2U * DSHOT_FRAME_BITS, &count),
		      -ENOMEM);
	zassert_equal(count, 0U);
}

ZTEST_SUITE(dshot_pio_encode, NULL, NULL, NULL, NULL, NULL);
