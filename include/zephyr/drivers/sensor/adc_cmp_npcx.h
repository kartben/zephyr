/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup sensor_adc_cmp_npcx
 * @brief API for NPCX ADC comparator sensor.
 */

#ifndef _ADC_CMP_NPCX_H_
#define _ADC_CMP_NPCX_H_

/**
 * @brief NPCX ADC Comparator Sensor API
 * @defgroup sensor_adc_cmp_npcx NPCX ADC Comparator Sensor
 * @ingroup adc_interface
 * @{
 */

/**
 * @brief ADC comparison modes
 *
 * Defines the comparison modes for the NPCX ADC comparator.
 */
enum adc_cmp_npcx_comparison {
	ADC_CMP_NPCX_GREATER,        /**< Trigger when value is greater than threshold */
	ADC_CMP_NPCX_LESS_OR_EQUAL,  /**< Trigger when value is less than or equal to threshold */
};

/**
 * @brief ADC threshold controllers
 *
 * Supported ADC threshold controllers in NPCX series.
 */
enum npcx_adc_cmp_thrctl {
	ADC_CMP_NPCX_THRCTL1,        /**< Threshold controller 1 */
	ADC_CMP_NPCX_THRCTL2,        /**< Threshold controller 2 */
	ADC_CMP_NPCX_THRCTL3,        /**< Threshold controller 3 */
	ADC_CMP_NPCX_THRCTL4,        /**< Threshold controller 4 */
	ADC_CMP_NPCX_THRCTL5,        /**< Threshold controller 5 */
	ADC_CMP_NPCX_THRCTL6,        /**< Threshold controller 6 */
	ADC_CMP_NPCX_THRCTL_COUNT,   /**< Number of threshold controllers */
};

/**
 * @brief ADC comparator sensor attributes
 *
 * Custom sensor attributes for the NPCX ADC comparator.
 */
enum adc_cmp_npcx_sensor_attribute {
	/** Lower voltage threshold attribute */
	SENSOR_ATTR_LOWER_VOLTAGE_THRESH = SENSOR_ATTR_PRIV_START,
	/** Upper voltage threshold attribute */
	SENSOR_ATTR_UPPER_VOLTAGE_THRESH,
};

/**
 * @}
 */

#endif /* _ADC_CMP_NPCX_H_ */
