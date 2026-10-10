/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the Futaba SBUS input driver.
 * @ingroup input_interface_ext
 */

#ifndef ZEPHYR_INCLUDE_INPUT_INPUT_SBUS_H_
#define ZEPHYR_INCLUDE_INPUT_INPUT_SBUS_H_

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @addtogroup input_interface_ext
 * @{
 */

/**
 * @brief SBUS receiver status.
 */
struct input_sbus_status {
	/** Failsafe flag of the last frame: the receiver lost the transmitter. */
	bool failsafe;
	/** Frame lost flag of the last frame: the receiver missed a frame. */
	bool frame_lost;
	/** No frame was received within the frame timeout since the last frame. */
	bool receiver_lost;
	/** Number of frames received with the frame lost flag set. */
	uint32_t frames_lost;
	/** Uptime in milliseconds when the last frame was received, 0 if none was. */
	int64_t last_frame_ms;
};

/**
 * @brief Get the status of an SBUS receiver.
 *
 * Applications can call it on each sync event of the device to know whether
 * the channel values of the frame come from the transmitter.
 *
 * @param dev SBUS input device.
 * @param status Status of the receiver.
 */
void input_sbus_get_status(const struct device *dev, struct input_sbus_status *status);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_INPUT_INPUT_SBUS_H_ */
