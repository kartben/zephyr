/**
 * @file
 * @brief Microchip MTCH9010 capacitive touch sensor Devicetree constants
 */

/*
 * Copyright (c) 2025 Microchip Technology Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_MTCH9010_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_MTCH9010_H_

/**
 * @defgroup mtch9010_dt_api Microchip MTCH9010 Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup mtch9010_operating_mode Operating mode
 * @{
 */
#define MTCH9010_CAPACITIVE 0x0 /**< Capacitive sensor mode */
#define MTCH9010_CONDUCTIVE 0x1 /**< Conductive sensor mode */
/** @} */

/**
 * @defgroup mtch9010_output_format UART output data format
 * @{
 */
#define MTCH9010_OUTPUT_FORMAT_DELTA               0x0 /**< Delta values */
#define MTCH9010_OUTPUT_FORMAT_CURRENT             0x1 /**< Current values */
#define MTCH9010_OUTPUT_FORMAT_BOTH                0x2 /**< Both delta and current */
#define MTCH9010_OUTPUT_FORMAT_MPLAB_DATA_VISUALIZER 0x3 /**< MPLAB Data Visualizer format */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_MTCH9010_H_ */
