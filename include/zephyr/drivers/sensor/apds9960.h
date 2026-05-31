/*
 * Copyright (c) 2017 Intel Corporation
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_APDS9960_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_APDS9960_H_

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sensor_channel_apds9960 {
	SENSOR_CHAN_APDS9960_GESTURE = SENSOR_CHAN_PRIV_START,
};

enum apds9960_gesture {
	APDS9960_GESTURE_NONE,
	APDS9960_GESTURE_UP,
	APDS9960_GESTURE_DOWN,
	APDS9960_GESTURE_LEFT,
	APDS9960_GESTURE_RIGHT,
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_APDS9960_H_ */
