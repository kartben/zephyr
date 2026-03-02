/*
 * Copyright (c) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for NPCX V2T sensor attributes.
 * @ingroup adc_v2t_npcx_interface
 */

#ifndef _ADC_V2T_NPCX_H_
#define _ADC_V2T_NPCX_H_

#include <zephyr/drivers/sensor.h>

/**
 * @brief NPCX V2T sensor extensions.
 * @defgroup adc_v2t_npcx_interface ADC V2T NPCX
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief NPCX V2T-specific sensor attributes.
 */
enum npcx_adc_v2t_sensor_attr {
	/** Select the V2T channel configuration to apply. */
	SENSOR_ATTR_NPCX_V2T_CHANNEL_CFG = SENSOR_ATTR_PRIV_START,
};

/**
 * @}
 */

#endif /* _ADC_V2T_NPCX_H_ */
