/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_DSHOT_DSHOT_PULSE_IO_WAVE_H_
#define ZEPHYR_DRIVERS_DSHOT_DSHOT_PULSE_IO_WAVE_H_

#include <stddef.h>
#include <stdint.h>

#include <zephyr/drivers/dshot.h>
#include <zephyr/drivers/pulse_io.h>

/* Fewest ticks per bit that keep the 3/8 and 3/4 high times apart */
#define DSHOT_PIO_MIN_BIT_TICKS 8U

/* Largest accepted bit rate deviation, in parts per million */
#define DSHOT_PIO_MAX_RATE_ERROR_PPM 20000

/*
 * Low time appended to every frame so that the next one is recognized as
 * a new frame: at least two bit periods and at least 2 us.
 */
#define DSHOT_PIO_GAP_MIN_BITS 2U
#define DSHOT_PIO_GAP_MIN_NS   2000U
#define DSHOT_PIO_GAP_MAX_BITS 3U

#define DSHOT_PIO_MAX_CELLS   (DSHOT_FRAME_BITS + DSHOT_PIO_GAP_MAX_BITS)
#define DSHOT_PIO_MAX_SYMBOLS (2U * DSHOT_FRAME_BITS + DSHOT_PIO_GAP_MAX_BITS)

/* Submission mode and tick rate to request from a Pulse IO channel */
struct dshot_pio_request {
	enum pulse_io_mode mode;
	uint32_t resolution_hz;
	/* Longest cell period or symbol the backend accepts, in ticks */
	uint32_t max_ticks;
};

/* Waveform of one DShot bit, quantized to the channel tick */
struct dshot_pio_timing {
	enum pulse_io_mode mode;
	uint32_t resolution_hz;
	uint32_t bit_ticks;
	uint32_t t0h_ticks;
	uint32_t t1h_ticks;
	/* Idle bit periods after the frame */
	uint32_t gap_bits;
	/* Deviation of the generated bit rate from the nominal one */
	int32_t rate_error_ppm;
};

/**
 * Pick the submission mode and tick rate for a channel from the backend
 * capabilities. CELL mode is preferred when the backend implements duty
 * cycles; level-only cells cannot encode DShot bits.
 *
 * Returns -ENOTSUP when the channel cannot transmit or no supported mode
 * reaches DSHOT_PIO_MIN_BIT_TICKS ticks per bit at @p bitrate.
 */
int dshot_pio_select(const struct pulse_io_caps *caps, uint8_t channel, uint32_t bitrate,
		     struct dshot_pio_request *req);

/**
 * Quantize the bit period and high times to the effective tick rate of a
 * configured channel.
 *
 * Returns -ENOTSUP when the bit period falls outside
 * [DSHOT_PIO_MIN_BIT_TICKS, max_ticks] or the bit rate is off by more
 * than DSHOT_PIO_MAX_RATE_ERROR_PPM.
 */
int dshot_pio_timing_init(struct dshot_pio_timing *t, enum pulse_io_mode mode, uint32_t bitrate,
			  uint32_t resolution_hz, uint32_t max_ticks);

/**
 * Expand a frame into one cell per bit, MSB first, followed by
 * gap_bits zero-duty cells.
 */
int dshot_pio_encode_cells(const struct dshot_pio_timing *t, uint16_t frame,
			   struct pulse_cell *cells, size_t cap, size_t *count);

/**
 * Expand a frame into a high and a low symbol per bit, MSB first,
 * followed by gap_bits low symbols of one bit period.
 */
int dshot_pio_encode_symbols(const struct dshot_pio_timing *t, uint16_t frame,
			     struct pulse_symbol *symbols, size_t cap, size_t *count);

#endif /* ZEPHYR_DRIVERS_DSHOT_DSHOT_PULSE_IO_WAVE_H_ */
