/**
 * @file
 * @brief Nuvoton NPCX tachometer Devicetree constants
 */

/*
 * Copyright (c) 2021 Nuvoton Technology Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_NPCX_TACH_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_NPCX_TACH_H_

/**
 * @defgroup npcx_tach_dt_api Nuvoton NPCX tachometer Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup npcx_tach_port Tachometer port selection
 * @{
 */
#define NPCX_TACH_PORT_A     0 /**< Port A */
#define NPCX_TACH_PORT_B     1 /**< Port B */
/** @} */

/**
 * @defgroup npcx_tach_freq Tachometer operating frequency
 * @{
 */
#define NPCX_TACH_FREQ_LFCLK 32768 /**< Low-frequency clock (32.768 kHz) */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_SENSOR_NPCX_TACH_H_ */
