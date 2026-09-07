/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SENSOR_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SENSOR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef CONFIG_SAMPLE_AA_SENSOR_CHANNEL

/** @brief Subscribe to the night mode sensor once the sensor channel is open. */
int aa_sensor_start(void);

/** @brief Handle a message received on the sensor channel. */
void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len);

#else

static inline int aa_sensor_start(void)
{
	return 0;
}

static inline void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len)
{
}

#endif /* CONFIG_SAMPLE_AA_SENSOR_CHANNEL */

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_SRC_AA_SENSOR_H_ */
