/*
 * Copyright (c) 2023 Trackunit Corporation
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for the GNSS RTK correction data callback API.
 * @ingroup gnss_interface
 */

#ifndef ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_
#define ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/** RTK correction data structure */
struct gnss_rtk_data {
	/** Correction data */
	const uint8_t *data;
	/** Length of the correction data, in bytes */
	size_t len;
};

/** Template for GNSS RTK correction data callback */
typedef void (*gnss_rtk_data_callback_t)(const struct device *dev,
					 const struct gnss_rtk_data *data);

/** GNSS RTK correction data callback structure */
struct gnss_rtk_data_callback {
	/** Device provided to the callback when it is invoked */
	const struct device *dev;
	/** Callback called when RTK correction data is published */
	gnss_rtk_data_callback_t callback;
};

/**
 * @brief Register a callback structure for GNSS RTK correction data published
 *
 * @param _dev Device pointer
 * @param _callback The callback function (see @ref gnss_rtk_data_callback_t)
 */
#if CONFIG_GNSS_RTK
#define GNSS_RTK_DATA_CALLBACK_DEFINE(_dev, _callback)						   \
	static const STRUCT_SECTION_ITERABLE(gnss_rtk_data_callback,				   \
					     _gnss_rtk_data_callback__##_callback) = {		   \
		.dev = _dev,									   \
		.callback = _callback,								   \
	}

#define GNSS_DT_RTK_DATA_CALLBACK_DEFINE(_node_id, _callback)                                      \
	static const STRUCT_SECTION_ITERABLE(                                                      \
		gnss_rtk_data_callback,                                                            \
		CONCAT(_gnss_rtk_data_callback_, DT_DEP_ORD(_node_id), _, _callback)) = {          \
		.dev = DEVICE_DT_GET(_node_id),                                                    \
		.callback = _callback,                                                             \
	}
#else
#define GNSS_RTK_DATA_CALLBACK_DEFINE(_dev, _callback)
/**
 * @brief Register a callback structure for GNSS RTK correction data published,
 *        by devicetree node.
 *
 * @param _node_id Devicetree node identifier of the GNSS device
 * @param _callback The callback function (see @ref gnss_rtk_data_callback_t)
 */
#define GNSS_DT_RTK_DATA_CALLBACK_DEFINE(_node_id, _callback)
#endif

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_GNSS_RTK_RTK_H_ */
