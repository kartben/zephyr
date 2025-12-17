/*
 * Copyright (c) 2023 The ChromiumOS Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup adc_voltage_divider
 * @brief API for ADC voltage divider.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_VOLTAGE_DIVIDER_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_VOLTAGE_DIVIDER_H_

/**
 * @brief ADC Voltage Divider API
 * @defgroup adc_voltage_divider ADC Voltage Divider
 * @ingroup adc_interface
 * @{
 */

#include <zephyr/drivers/adc.h>

/**
 * @brief Voltage divider configuration structure
 *
 * Contains the configuration parameters for a voltage divider
 * connected to an ADC.
 */
struct voltage_divider_dt_spec {
	const struct adc_dt_spec port;  /**< ADC port configuration */
	uint32_t full_ohms;             /**< Full resistance of voltage divider in ohms */
	uint32_t output_ohms;           /**< Output resistance of voltage divider in ohms */
};

/**
 * @brief Get voltage divider information from devicetree.
 *
 * This returns a static initializer for a @p voltage_divider_dt_spec structure
 * given a devicetree node.
 *
 * @param node_id Devicetree node identifier.
 *
 * @return Static initializer for a voltage_divider_dt_spec structure.
 */
#define VOLTAGE_DIVIDER_DT_SPEC_GET(node_id)                                                       \
	{                                                                                          \
		.port = ADC_DT_SPEC_GET(node_id),                                                  \
		.full_ohms = DT_PROP_OR(node_id, full_ohms, 0),                                    \
		.output_ohms = DT_PROP(node_id, output_ohms),                                      \
	}

/**
 * @brief Calculates the actual voltage from the measured voltage
 *
 * Scales the measured voltage based on the voltage divider ratio to determine
 * the actual input voltage.
 *
 * @param[in] spec voltage divider specification from Devicetree.
 * @param[in,out] v_to_v Pointer to the measured voltage on input, and the
 * corresponding scaled voltage value on output.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if "full_ohms" is not specified.
 */
static inline int voltage_divider_scale_dt(const struct voltage_divider_dt_spec *spec,
					   int32_t *v_to_v)
{
	/* cannot be scaled if "full_ohms" is not specified */
	if (spec->full_ohms == 0) {
		return -ENOTSUP;
	}

	/* voltage scaled by voltage divider values using DT binding */
	*v_to_v = (int64_t)*v_to_v * spec->full_ohms / spec->output_ohms;

	return 0;
}

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_VOLTAGE_DIVIDER_H_ */
