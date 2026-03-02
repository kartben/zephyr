/**
 * @file
 * @brief ST LIS2DH accelerometer Devicetree constants
 */

/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DH_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DH_H_

/**
 * @defgroup lis2dh_dt_api ST LIS2DH Devicetree options
 * @ingroup sensor_interface
 * @{
 */

/**
 * @defgroup lis2dh_gpio_int GPIO interrupt trigger mode
 * @{
 */
#define LIS2DH_DT_GPIO_INT_EDGE_BOTH		0 /**< Both edges */
#define LIS2DH_DT_GPIO_INT_EDGE_RISING		1 /**< Rising edge */
#define LIS2DH_DT_GPIO_INT_EDGE_FALLING		2 /**< Falling edge */
#define LIS2DH_DT_GPIO_INT_LEVEL_HIGH		3 /**< Level high */
#define LIS2DH_DT_GPIO_INT_LEVEL_LOW		4 /**< Level low */
/** @} */

/**
 * @defgroup lis2dh_anym_mode Any-motion (slope) interrupt mode
 * @{
 */
#define LIS2DH_DT_ANYM_OR_COMBINATION		0 /**< OR combination of interrupt events */
#define LIS2DH_DT_ANYM_6D_MOVEMENT		1 /**< 6-direction movement recognition */
#define LIS2DH_DT_ANYM_AND_COMBINATION		2 /**< AND combination of interrupt events */
#define LIS2DH_DT_ANYM_6D_POSITION		3 /**< 6-direction position recognition */
/** @} */

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_ST_LIS2DH_H_ */
