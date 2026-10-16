/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "dshot_pulse_io_wave.h"

#include <errno.h>

#include <zephyr/sys/clock.h>
#include <zephyr/sys/util.h>

/* Width of the duration field of struct pulse_symbol */
#define DSHOT_PIO_SYMBOL_TICKS_MAX BIT_MASK(31)

/* Width of the duty field of struct pulse_cell */
#define DSHOT_PIO_CELL_DUTY_BITS_MAX 16U

static int dshot_pio_request_mode(const struct pulse_io_caps *caps, enum pulse_io_mode mode,
				  uint32_t bitrate, struct dshot_pio_request *req)
{
	uint32_t max_ticks = DSHOT_PIO_SYMBOL_TICKS_MAX;
	uint64_t resolution;

	if (caps->max_duration_ticks != 0U) {
		max_ticks = MIN(max_ticks, caps->max_duration_ticks);
	}
	if (mode == PULSE_IO_MODE_CELL) {
		/* The cell period has to fit in the duty field as well */
		max_ticks = MIN(max_ticks,
				BIT_MASK(MIN(caps->cell_duty_bits, DSHOT_PIO_CELL_DUTY_BITS_MAX)));
	}

	/* Finest tick on offer, capped where the bit period stops fitting */
	resolution = MIN((uint64_t)NSEC_PER_SEC / caps->min_tick_ns, (uint64_t)bitrate * max_ticks);

	if (resolution < (uint64_t)bitrate * DSHOT_PIO_MIN_BIT_TICKS) {
		return -ENOTSUP;
	}
	if ((caps->max_tick_ns != 0U) && (resolution * caps->max_tick_ns < NSEC_PER_SEC)) {
		return -ENOTSUP;
	}

	*req = (struct dshot_pio_request){
		.mode = mode,
		.resolution_hz = (uint32_t)resolution,
		.max_ticks = max_ticks,
	};

	return 0;
}

int dshot_pio_select(const struct pulse_io_caps *caps, uint8_t channel, uint32_t bitrate,
		     struct dshot_pio_request *req)
{
	if ((caps == NULL) || (req == NULL) || (bitrate == 0U)) {
		return -EINVAL;
	}
	if (!caps->supports_tx || (channel >= 32U) ||
	    ((caps->tx_channel_mask & BIT(channel)) == 0U)) {
		return -ENOTSUP;
	}
	if (caps->min_tick_ns == 0U) {
		return -ENOTSUP;
	}

	if (((caps->modes & PULSE_IO_MODE_CELL) != 0U) && (caps->cell_duty_bits > 0U) &&
	    (dshot_pio_request_mode(caps, PULSE_IO_MODE_CELL, bitrate, req) == 0)) {
		return 0;
	}
	if ((caps->modes & PULSE_IO_MODE_SYMBOL) != 0U) {
		return dshot_pio_request_mode(caps, PULSE_IO_MODE_SYMBOL, bitrate, req);
	}

	return -ENOTSUP;
}

int dshot_pio_timing_init(struct dshot_pio_timing *t, enum pulse_io_mode mode, uint32_t bitrate,
			  uint32_t resolution_hz, uint32_t max_ticks)
{
	uint64_t bit_ticks;
	int64_t nominal_hz;
	int64_t error_ppm;

	if ((t == NULL) || (bitrate == 0U) || (resolution_hz == 0U)) {
		return -EINVAL;
	}

	bit_ticks = DIV_ROUND_CLOSEST((uint64_t)resolution_hz, (uint64_t)bitrate);
	if ((bit_ticks < DSHOT_PIO_MIN_BIT_TICKS) || (bit_ticks > max_ticks)) {
		return -ENOTSUP;
	}

	/* The bit rate generated is resolution_hz / bit_ticks */
	nominal_hz = (int64_t)bitrate * (int64_t)bit_ticks;
	error_ppm = (((int64_t)resolution_hz - nominal_hz) * 1000000) / nominal_hz;
	if ((error_ppm > DSHOT_PIO_MAX_RATE_ERROR_PPM) ||
	    (error_ppm < -DSHOT_PIO_MAX_RATE_ERROR_PPM)) {
		return -ENOTSUP;
	}

	*t = (struct dshot_pio_timing){
		.mode = mode,
		.resolution_hz = resolution_hz,
		.bit_ticks = (uint32_t)bit_ticks,
		.t0h_ticks = (uint32_t)DIV_ROUND_CLOSEST(bit_ticks * 3U, 8U),
		.t1h_ticks = (uint32_t)DIV_ROUND_CLOSEST(bit_ticks * 3U, 4U),
		.gap_bits = MAX(DSHOT_PIO_GAP_MIN_BITS,
				(uint32_t)DIV_ROUND_UP((uint64_t)DSHOT_PIO_GAP_MIN_NS * bitrate,
						       NSEC_PER_SEC)),
		.rate_error_ppm = (int32_t)error_ppm,
	};

	return 0;
}

static uint32_t dshot_pio_high_ticks(const struct dshot_pio_timing *t, uint16_t frame, uint32_t bit)
{
	/* Bit 15 goes first */
	return ((frame & BIT(DSHOT_FRAME_BITS - 1U - bit)) != 0U) ? t->t1h_ticks : t->t0h_ticks;
}

int dshot_pio_encode_cells(const struct dshot_pio_timing *t, uint16_t frame,
			   struct pulse_cell *cells, size_t cap, size_t *count)
{
	size_t n = 0;

	if (cap < (DSHOT_FRAME_BITS + t->gap_bits)) {
		return -ENOMEM;
	}

	for (uint32_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
		cells[n++] = (struct pulse_cell){
			.duty = (uint16_t)dshot_pio_high_ticks(t, frame, bit),
		};
	}
	for (uint32_t gap = 0; gap < t->gap_bits; gap++) {
		cells[n++] = (struct pulse_cell){.duty = 0U};
	}

	*count = n;
	return 0;
}

int dshot_pio_encode_symbols(const struct dshot_pio_timing *t, uint16_t frame,
			     struct pulse_symbol *symbols, size_t cap, size_t *count)
{
	size_t n = 0;

	if (cap < ((2U * DSHOT_FRAME_BITS) + t->gap_bits)) {
		return -ENOMEM;
	}

	for (uint32_t bit = 0; bit < DSHOT_FRAME_BITS; bit++) {
		uint32_t high = dshot_pio_high_ticks(t, frame, bit);

		symbols[n++] = (struct pulse_symbol){.duration = high, .level = 1U};
		symbols[n++] = (struct pulse_symbol){.duration = t->bit_ticks - high, .level = 0U};
	}
	for (uint32_t gap = 0; gap < t->gap_bits; gap++) {
		symbols[n++] = (struct pulse_symbol){.duration = t->bit_ticks, .level = 0U};
	}

	*count = n;
	return 0;
}
