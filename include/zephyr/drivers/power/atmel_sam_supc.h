/*
 * Copyright (c) 2023 Bjarki Arge Andreasen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup io_interfaces
 * @brief Devicetree helpers for the Atmel SAM SUPC controller.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_

/** @brief Get the SUPC controller device from devicetree. */
#define SAM_DT_SUPC_CONTROLLER DEVICE_DT_GET(DT_NODELABEL(supc))

/**
 * @brief Get a SUPC wakeup-source identifier from a devicetree node.
 *
 * @param node_id Devicetree node identifier.
 */
#define SAM_DT_SUPC_WAKEUP_SOURCE_ID(node_id) \
	DT_PROP_BY_IDX(node_id, wakeup_source_id wakeup_source_id)

/**
 * @brief Get a SUPC wakeup-source identifier from a driver instance.
 *
 * @param inst Driver instance number.
 */
#define SAM_DT_INST_SUPC_WAKEUP_SOURCE_ID(inst) \
	SAM_DT_SUPC_WAKEUP_SOURCE_ID(DT_DRV_INST(inst))

#endif /* ZEPHYR_INCLUDE_DRIVERS_POWER_ATMEL_SAM_SUPC_H_ */
