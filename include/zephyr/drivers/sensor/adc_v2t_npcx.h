/*
 * Copyright (c) 2025 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor attributes of the Nuvoton NPCX ADC V2T sensor.
 * @ingroup sensor_interface_ext_nuvoton
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_V2T_NPCX_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_V2T_NPCX_H_

#include <zephyr/drivers/sensor.h>

/** @brief Additional sensor attributes for the NPCX ADC V2T sensor. */
enum npcx_adc_v2t_sensor_attr {
	/** Bitmask of ADC channels routed to the V2T module. */
	SENSOR_ATTR_NPCX_V2T_CHANNEL_CFG = SENSOR_ATTR_PRIV_START,
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_V2T_NPCX_H_ */
