/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Handle a message on the sensor channel. */
void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_ */
