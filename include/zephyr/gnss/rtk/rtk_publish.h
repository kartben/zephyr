/*
 * Copyright (c) 2025 Croxel Inc.
 * Copyright (c) 2025 CogniPilot Foundation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup gnss_interface
 * @brief RTK correction publishing helpers.
 */

#ifndef ZEPHYR_INCLUDE_GNSS_RTK_PUBLISH_H_
#define ZEPHYR_INCLUDE_GNSS_RTK_PUBLISH_H_

#include <stdint.h>
#include <stddef.h>
#include <zephyr/gnss/rtk/rtk.h>

/**
 * @brief Publish RTK correction data.
 *
 * @param data Correction payload descriptor.
 */
void gnss_rtk_publish_data(const struct gnss_rtk_data *data);

#endif /* ZEPHYR_INCLUDE_GNSS_RTK_PUBLISH_H_ */
