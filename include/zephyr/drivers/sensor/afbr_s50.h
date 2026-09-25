/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor API of AFBR-S50 sensor
 * @ingroup afbr_s50_interface
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_AFBR_S50_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_AFBR_S50_H_

/**
 * @brief Broadcom AFBR-S50 3D ToF sensor
 * @defgroup afbr_s50_interface AFBR-S50
 * @ingroup sensor_interface_ext_brcm
 * @{
 */

#include <zephyr/drivers/sensor.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Disregard pixel reading if contains this value */
#define AFBR_PIXEL_INVALID_VALUE 0x80000000

/**
 * Custom sensor channels for AFBR-S50
 */
enum sensor_channel_afbr_s50 {
	/**
	 * Distances of the 32 pixels of the 4 x 8 matrix, in meters.
	 *
	 * The decoder returns one reading per pixel: reading n holds pixel n. A pixel without
	 * a valid measurement reads as @ref AFBR_PIXEL_INVALID_VALUE.
	 */
	SENSOR_CHAN_AFBR_S50_PIXELS = SENSOR_CHAN_PRIV_START + 1,
};

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_AFBR_S50_H_ */
