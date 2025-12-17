/*
 * Copyright (c) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup sensor_adc_v2t_npcx
 * @brief API for NPCX ADC Voltage-to-Temperature sensor.
 */

#ifndef _ADC_V2T_NPCX_H_
#define _ADC_V2T_NPCX_H_

/**
 * @brief NPCX ADC V2T Sensor API
 * @defgroup sensor_adc_v2t_npcx NPCX ADC V2T Sensor
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/drivers/sensor.h>

/**
 * @brief V2T sensor attributes
 *
 * Custom sensor attributes for the NPCX ADC V2T sensor.
 */
enum npcx_adc_v2t_sensor_attr {
	/** V2T channel configuration attribute */
	SENSOR_ATTR_NPCX_V2T_CHANNEL_CFG = SENSOR_ATTR_PRIV_START,
};

/**
 * @}
 */

#endif /* _ADC_V2T_NPCX_H_ */
