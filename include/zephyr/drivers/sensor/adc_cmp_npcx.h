/*
 * Copyright (c) 2022 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public API extensions for NPCX ADC threshold comparison.
 * @ingroup adc_cmp_npcx_interface
 */

#ifndef _ADC_CMP_NPCX_H_
#define _ADC_CMP_NPCX_H_

/**
 * @brief NPCX ADC threshold comparison extensions.
 * @defgroup adc_cmp_npcx_interface ADC CMP NPCX
 * @ingroup sensor_interface_ext
 * @{
 */

/**
 * @brief Comparison modes for NPCX ADC threshold checks.
 */
enum adc_cmp_npcx_comparison {
	/** Trigger when the sampled value is greater than the threshold. */
	ADC_CMP_NPCX_GREATER,
	/** Trigger when the sampled value is less than or equal to the threshold. */
	ADC_CMP_NPCX_LESS_OR_EQUAL,
};

/**
 * @brief Supported ADC threshold controller indices in NPCX devices.
 */
enum npcx_adc_cmp_thrctl {
	/** Select threshold controller 1. */
	ADC_CMP_NPCX_THRCTL1,
	/** Select threshold controller 2. */
	ADC_CMP_NPCX_THRCTL2,
	/** Select threshold controller 3. */
	ADC_CMP_NPCX_THRCTL3,
	/** Select threshold controller 4. */
	ADC_CMP_NPCX_THRCTL4,
	/** Select threshold controller 5. */
	ADC_CMP_NPCX_THRCTL5,
	/** Select threshold controller 6. */
	ADC_CMP_NPCX_THRCTL6,
	/** Number of supported threshold controllers. */
	ADC_CMP_NPCX_THRCTL_COUNT,
};

/**
 * @brief NPCX ADC comparator-specific sensor attributes.
 */
enum adc_cmp_npcx_sensor_attribute {
	/** Set the lower threshold voltage. */
	SENSOR_ATTR_LOWER_VOLTAGE_THRESH = SENSOR_ATTR_PRIV_START,
	/** Set the upper threshold voltage. */
	SENSOR_ATTR_UPPER_VOLTAGE_THRESH,
};

/**
 * @}
 */

#endif /* _ADC_CMP_NPCX_H_ */
