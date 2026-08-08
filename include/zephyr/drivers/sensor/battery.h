/*
 * Copyright 2024 Embeint Inc
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for battery chemistry and open circuit voltage table helpers.
 * @ingroup battery_apis
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_SENSOR_BATTERY_H_
#define ZEPHYR_INCLUDE_DRIVERS_SENSOR_BATTERY_H_

#include <stdint.h>
#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/math/interpolation.h>
#include <zephyr/sys/util_macro.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Battery API
 * @defgroup battery_apis Battery APIs
 * @ingroup sensor_interface
 * @{
 */

/**
 * @brief Battery chemistry
 *
 * Value names must match those from dts/bindings/battery.yaml
 */
enum battery_chemistry {
	/** Unknown chemistry */
	BATTERY_CHEMISTRY_UNKNOWN = 0,
	/** Nickel-cadmium (NiCd) */
	BATTERY_CHEMISTRY_NICKEL_CADMIUM,
	/** Nickel-metal hydride (NiMH) */
	BATTERY_CHEMISTRY_NICKEL_METAL_HYDRIDE,
	/** Lithium-ion, blanket type for all lithium-ion based chemistries */
	BATTERY_CHEMISTRY_LITHIUM_ION,
	/** Lithium-ion polymer */
	BATTERY_CHEMISTRY_LITHIUM_ION_POLYMER,
	/** Lithium iron phosphate */
	BATTERY_CHEMISTRY_LITHIUM_ION_IRON_PHOSPHATE,
	/** Lithium-ion manganese oxide */
	BATTERY_CHEMISTRY_LITHIUM_ION_MANGANESE_OXIDE,
};

/**
 * @brief Length of an open circuit voltage table
 *
 * Open circuit voltage tables hold one entry per 10% state of charge step,
 * covering 0% to 100% charge.
 */
#define BATTERY_OCV_TABLE_LEN 11

/**
 * @brief Get the battery chemistry enum value
 *
 * @param node_id node identifier
 */
#define BATTERY_CHEMISTRY_DT_GET(node_id)                                                          \
	UTIL_CAT(BATTERY_CHEMISTRY_, DT_STRING_UPPER_TOKEN_OR(node_id, device_chemistry, UNKNOWN))

/**
 * @brief Get the OCV curve for a given table
 *
 * @param node_id node identifier
 * @param table table to retrieve
 */
#define BATTERY_OCV_TABLE_DT_GET(node_id, table)                                                   \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, table),                                              \
		    ({DT_FOREACH_PROP_ELEM_SEP(node_id, table, DT_PROP_BY_IDX, (,))}), ({-1}))

/**
 * @brief Convert an OCV table and battery voltage to a charge percentage
 *
 * @param ocv_table Open circuit voltage curve
 * @param voltage_uv Battery voltage in microVolts
 *
 * @returns Battery state of charge in milliPercent
 */
static inline int32_t battery_soc_lookup(const int32_t ocv_table[BATTERY_OCV_TABLE_LEN],
					 uint32_t voltage_uv)
{
	static const int32_t soc_axis[BATTERY_OCV_TABLE_LEN] = {
		0, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000};

	/* Convert voltage to SoC */
	return linear_interpolate(ocv_table, soc_axis, BATTERY_OCV_TABLE_LEN, voltage_uv);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_SENSOR_BATTERY_H_ */
