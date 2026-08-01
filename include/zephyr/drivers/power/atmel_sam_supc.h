/*
 * Copyright (c) 2023 Bjarki Arge Andreasen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file for Atmel SAM SUPC (Supply Controller) Devicetree helpers.
 * @ingroup sys_poweroff
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_

/** Get the device handle of the SUPC controller node. */
#define SAM_DT_SUPC_CONTROLLER DEVICE_DT_GET(DT_NODELABEL(supc))

/**
 * @brief Get the SUPC wakeup source ID from a node's wakeup-source-id property.
 *
 * @param node_id Node identifier.
 */
#define SAM_DT_SUPC_WAKEUP_SOURCE_ID(node_id) \
	DT_PROP_BY_IDX(node_id, wakeup_source_id wakeup_source_id)

/**
 * @brief Get the SUPC wakeup source ID from a DT_DRV_COMPAT instance.
 *
 * @param inst Instance number.
 */
#define SAM_DT_INST_SUPC_WAKEUP_SOURCE_ID(inst) \
	SAM_DT_SUPC_WAKEUP_SOURCE_ID(DT_DRV_INST(inst))

#endif /* ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_ */
