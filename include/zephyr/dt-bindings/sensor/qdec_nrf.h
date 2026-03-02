/**
 * @file
 * @brief Nordic nRF QDEC quadrature decoder Devicetree constants
 */

/*
 * Copyright 2025 Mark Geiger <MarkGeiger@posteo.de>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_NRF_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_NRF_H_

/**
 * @defgroup qdec_nrf_dt_api Nordic nRF QDEC Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup qdec_nrf_sampleper Sample period
 * @{
 */
#define SAMPLEPER_128US         0 /**< 128 µs */
#define SAMPLEPER_256US         1 /**< 256 µs */
#define SAMPLEPER_512US         2 /**< 512 µs */
#define SAMPLEPER_1024US        3 /**< 1024 µs */
#define SAMPLEPER_2048US        4 /**< 2048 µs */
#define SAMPLEPER_4096US        5 /**< 4096 µs */
#define SAMPLEPER_8192US        6 /**< 8192 µs */
#define SAMPLEPER_16384US       7 /**< 16384 µs */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_NRF_H_ */
