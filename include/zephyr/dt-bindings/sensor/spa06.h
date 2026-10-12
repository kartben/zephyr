/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Devicetree binding constants for the Goertek SPA06-003 pressure sensor.
 * @ingroup spa06_interface
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_SPA06_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_SPA06_H_

/**
 * @defgroup spa06_interface SPA06-003
 * @ingroup sensor_interface_ext_goertek
 * @brief Goertek SPA06-003 barometric pressure and temperature sensor
 * @{
 */

/**
 * @name Measurement rates
 *
 * Values for the `pressure-odr` and `temperature-odr` devicetree properties.
 * They are the register encodings of the rates in continuous mode.
 * @{
 */
#define SPA06_DT_ODR_1      0x0 /**< 1 Hz */
#define SPA06_DT_ODR_2      0x1 /**< 2 Hz */
#define SPA06_DT_ODR_4      0x2 /**< 4 Hz */
#define SPA06_DT_ODR_8      0x3 /**< 8 Hz */
#define SPA06_DT_ODR_16     0x4 /**< 16 Hz */
#define SPA06_DT_ODR_32     0x5 /**< 32 Hz */
#define SPA06_DT_ODR_64     0x6 /**< 64 Hz */
#define SPA06_DT_ODR_128    0x7 /**< 128 Hz */
#define SPA06_DT_ODR_1_5625 0x8 /**< 1.5625 Hz, pressure only */
#define SPA06_DT_ODR_3_125  0x9 /**< 3.125 Hz */
#define SPA06_DT_ODR_6_25   0xA /**< 6.25 Hz */
#define SPA06_DT_ODR_12_5   0xB /**< 12.5 Hz */
#define SPA06_DT_ODR_25     0xC /**< 25 Hz */
#define SPA06_DT_ODR_50     0xD /**< 50 Hz */
#define SPA06_DT_ODR_100    0xE /**< 100 Hz */
#define SPA06_DT_ODR_200    0xF /**< 200 Hz */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_SPA06_H_ */
