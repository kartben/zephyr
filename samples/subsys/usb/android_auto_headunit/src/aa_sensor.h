/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_
#define SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** @brief Handle a message on the sensor channel. */
void aa_sensor_handle(uint16_t msg_id, const uint8_t *body, size_t len);

/**
 * @brief Report how light it is around the head unit.
 *
 * The phone redraws what it projects in its night theme while @p night holds.
 */
void aa_sensor_set_night(bool night);

/** @brief Forget which sensors the phone had started, it has gone. */
void aa_sensor_link_down(void);

#endif /* SAMPLES_SUBSYS_USB_ANDROID_AUTO_HEADUNIT_SRC_AA_SENSOR_H_ */
