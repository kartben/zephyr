/*
 * Copyright (c) 2023 Trackunit Corporation
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup gnss_interface
 * @brief Real-time kinematic GNSS callback registration helpers.
 */

#ifndef ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_
#define ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/** @brief RTK correction payload descriptor. */
struct gnss_rtk_data {
	/** Pointer to correction data bytes. */
	const uint8_t *data;
	/** Length of @ref data, in bytes. */
	size_t len;
};

/**
 * @brief Handle RTK correction data.
 *
 * @param dev GNSS device that receives the correction data.
 * @param data Correction payload descriptor.
 */
typedef void (*gnss_rtk_data_callback_t)(const struct device *dev,
					 const struct gnss_rtk_data *data);

/** @brief RTK callback registration entry. */
struct gnss_rtk_data_callback {
	/** GNSS device to match, or @c NULL to accept all devices. */
	const struct device *dev;
	/** Callback invoked when correction data is published. */
	gnss_rtk_data_callback_t callback;
};

#if CONFIG_GNSS_RTK
/**
 * @brief Register an RTK callback for a specific device.
 *
 * @param _dev GNSS device pointer used for callback filtering.
 * @param _callback Callback function to invoke on RTK data publication.
 */
#define GNSS_RTK_DATA_CALLBACK_DEFINE(_dev, _callback)						   \
	static const STRUCT_SECTION_ITERABLE(gnss_rtk_data_callback,				   \
					     _gnss_rtk_data_callback__##_callback) = {		   \
		.dev = _dev,									   \
		.callback = _callback,								   \
	}

/**
 * @brief Register an RTK callback using a devicetree node identifier.
 *
 * @param _node_id Devicetree node identifier of the GNSS device.
 * @param _callback Callback function to invoke on RTK data publication.
 */
#define GNSS_DT_RTK_DATA_CALLBACK_DEFINE(_node_id, _callback)                                      \
	static const STRUCT_SECTION_ITERABLE(                                                      \
		gnss_rtk_data_callback,                                                            \
		CONCAT(_gnss_rtk_data_callback_, DT_DEP_ORD(_node_id), _, _callback)) = {          \
		.dev = DEVICE_DT_GET(_node_id),                                                    \
		.callback = _callback,                                                             \
	}
#else
/** @cond INTERNAL_HIDDEN */
#define GNSS_RTK_DATA_CALLBACK_DEFINE(_dev, _callback)
#define GNSS_DT_RTK_DATA_CALLBACK_DEFINE(_node_id, _callback)
/** @endcond */
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_ */
