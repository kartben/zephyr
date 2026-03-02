/**
 * @file
 * @brief STM32 QDEC quadrature decoder Devicetree constants
 */

/*
 * Copyright 2022 Valerio Setti <vsetti@baylibre.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_STM32_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_STM32_H_

/**
 * @defgroup qdec_stm32_dt_api STM32 QDEC Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup qdec_stm32_filter Input filter clock divider and sample count
 * @{
 */
#define NO_FILTER	0  /**< No filtering */
#define FDIV1_N2	1  /**< f_DTS/1, N=2  */
#define FDIV1_N4	2  /**< f_DTS/1, N=4  */
#define FDIV1_N8	3  /**< f_DTS/1, N=8  */
#define FDIV2_N6	4  /**< f_DTS/2, N=6  */
#define FDIV2_N8	5  /**< f_DTS/2, N=8  */
#define FDIV4_N6	6  /**< f_DTS/4, N=6  */
#define FDIV4_N8	7  /**< f_DTS/4, N=8  */
#define FDIV8_N6	8  /**< f_DTS/8, N=6  */
#define FDIV8_N8	9  /**< f_DTS/8, N=8  */
#define FDIV16_N5	10 /**< f_DTS/16, N=5 */
#define FDIV16_N6	11 /**< f_DTS/16, N=6 */
#define FDIV16_N8	12 /**< f_DTS/16, N=8 */
#define FDIV32_N5	13 /**< f_DTS/32, N=5 */
#define FDIV32_N6	14 /**< f_DTS/32, N=6 */
#define FDIV32_N8	15 /**< f_DTS/32, N=8 */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_QDEC_STM32_H_ */
