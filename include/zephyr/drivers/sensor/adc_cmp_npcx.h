/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for extended sensor attributes of the Nuvoton NPCX ADC comparator.
 * @ingroup sensor_interface_ext_nuvoton
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_CMP_NPCX_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_CMP_NPCX_H_

/** @brief Comparison condition between the measured value and the threshold. */
enum adc_cmp_npcx_comparison {
	/** Event triggers when the value is greater than the threshold. */
	ADC_CMP_NPCX_GREATER,
	/** Event triggers when the value is less than or equal to the threshold. */
	ADC_CMP_NPCX_LESS_OR_EQUAL,
};

/** @brief Supported ADC threshold controllers in NPCX series. */
enum npcx_adc_cmp_thrctl {
	ADC_CMP_NPCX_THRCTL1,      /**< Threshold controller 1. */
	ADC_CMP_NPCX_THRCTL2,      /**< Threshold controller 2. */
	ADC_CMP_NPCX_THRCTL3,      /**< Threshold controller 3. */
	ADC_CMP_NPCX_THRCTL4,      /**< Threshold controller 4. */
	ADC_CMP_NPCX_THRCTL5,      /**< Threshold controller 5. */
	ADC_CMP_NPCX_THRCTL6,      /**< Threshold controller 6. */
	ADC_CMP_NPCX_THRCTL_COUNT, /**< Number of threshold controllers. */
};

/** @brief Additional sensor attributes for the NPCX ADC comparator. */
enum adc_cmp_npcx_sensor_attribute {
	/** Lower voltage threshold, in millivolts. */
	SENSOR_ATTR_LOWER_VOLTAGE_THRESH = SENSOR_ATTR_PRIV_START,
	/** Upper voltage threshold, in millivolts. */
	SENSOR_ATTR_UPPER_VOLTAGE_THRESH,
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_ADC_CMP_NPCX_H_ */
