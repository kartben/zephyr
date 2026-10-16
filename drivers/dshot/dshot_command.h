/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_DSHOT_DSHOT_COMMAND_H_
#define ZEPHYR_DRIVERS_DSHOT_DSHOT_COMMAND_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/dshot.h>

/* Pause between two repetitions of a command frame */
#define DSHOT_COMMAND_INTERVAL_US 1000U

/* How a command has to be sent for an ESC to act on it */
struct dshot_command_info {
	/* Number of identical consecutive frames */
	uint8_t repeat;
	/* Telemetry request bit of the frames */
	bool telemetry;
	/* Time to leave the ESC before the next frame, in microseconds */
	uint32_t delay_us;
};

void dshot_command_get_info(enum dshot_command cmd, struct dshot_command_info *info);

#endif /* ZEPHYR_DRIVERS_DSHOT_DSHOT_COMMAND_H_ */
