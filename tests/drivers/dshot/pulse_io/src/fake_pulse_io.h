/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef FAKE_PULSE_IO_H_
#define FAKE_PULSE_IO_H_

#include <zephyr/device.h>
#include <zephyr/drivers/pulse_io.h>
#include <zephyr/kernel.h>

#define FAKE_PIO_MAX_CHANNELS 8
#define FAKE_PIO_MAX_ENTRIES  64
#define FAKE_PIO_HISTORY      32

struct fake_pio_channel {
	bool in_use;
	bool configured;
	struct pulse_io_config cfg;
	/* Tick rate the channel runs at after rounding */
	uint32_t resolution_hz;
	uint32_t configure_count;

	/* Last transmission, copied when the transmit call completes */
	uint32_t tx_count;
	size_t last_count;
	union {
		struct pulse_cell cells[FAKE_PIO_MAX_ENTRIES];
		struct pulse_symbol symbols[FAKE_PIO_MAX_ENTRIES];
	} last;
	k_timeout_t last_timeout;

	/*
	 * First 16 bits of every transmission, read back by comparing the
	 * high time of each bit with half its period
	 */
	uint16_t history[FAKE_PIO_HISTORY];
	size_t history_len;

	/* Returned by every transmission while nonzero */
	int tx_error;
	/* When set, a transmission waits for it before completing */
	struct k_sem *tx_gate;
	volatile bool tx_waiting;
};

struct fake_pio_channel *fake_pio_channel(const struct device *dev, uint8_t index);

/* Capabilities reported from now on; channels keep their configuration */
struct pulse_io_caps *fake_pio_caps(const struct device *dev);

/* Effective rate is src_clk_hz divided by the nearest integer */
void fake_pio_set_src_clock(const struct device *dev, uint32_t src_clk_hz);

/* Make pulse_io_channel_get_resolution() return -ENOSYS */
void fake_pio_set_readback(const struct device *dev, bool supported);

/* Restore the default capabilities and clear recordings and faults */
void fake_pio_reset(const struct device *dev);

#endif /* FAKE_PULSE_IO_H_ */
