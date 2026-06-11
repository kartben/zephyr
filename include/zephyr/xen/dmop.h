/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Xen device-model operations.
 * @ingroup xen_device_model
 */

#ifndef ZEPHYR_XEN_DMOP_H_
#define ZEPHYR_XEN_DMOP_H_

#include <zephyr/xen/public/hvm/dm_op.h>

/**
 * @defgroup xen_device_model Xen device model
 * @ingroup xen_support
 * @brief Manage Xen I/O request servers and related device-model state.
 * @{
 */

/**
 * @brief Create an I/O request server for a domain.
 *
 * @param domid Domain that owns the server.
 * @param handle_bufioreq Set to a non-zero value to enable buffered I/O requests.
 * @param[out] id Buffer that receives the server identifier assigned by Xen.
 *
 * @retval 0 If the server was created successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_create_ioreq_server(domid_t domid, uint8_t handle_bufioreq, ioservid_t *id);

/**
 * @brief Destroy an I/O request server.
 *
 * @param domid Domain that owns the server.
 * @param id Server identifier returned by dmop_create_ioreq_server().
 *
 * @retval 0 If the server was destroyed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_destroy_ioreq_server(domid_t domid, ioservid_t id);

/**
 * @brief Route an I/O range to an I/O request server.
 *
 * @param domid Domain that owns the server.
 * @param id Server identifier returned by dmop_create_ioreq_server().
 * @param type Xen I/O range type.
 * @param start Inclusive start of the range.
 * @param end Inclusive end of the range.
 *
 * @retval 0 If the mapping was created successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_map_io_range_to_ioreq_server(domid_t domid, ioservid_t id,
				      uint32_t type, uint64_t start,
				      uint64_t end);

/**
 * @brief Remove an I/O range from an I/O request server.
 *
 * @param domid Domain that owns the server.
 * @param id Server identifier returned by dmop_create_ioreq_server().
 * @param type Xen I/O range type.
 * @param start Inclusive start of the range.
 * @param end Inclusive end of the range.
 *
 * @retval 0 If the mapping was removed successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_unmap_io_range_from_ioreq_server(domid_t domid, ioservid_t id, uint32_t type,
					  uint64_t start, uint64_t end);

/**
 * @brief Enable or disable an I/O request server.
 *
 * @param domid Domain that owns the server.
 * @param id Server identifier returned by dmop_create_ioreq_server().
 * @param enabled Set to non-zero to enable the server, or zero to disable it.
 *
 * @retval 0 If the state update succeeded.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_set_ioreq_server_state(domid_t domid, ioservid_t id, uint8_t enabled);

/**
 * @brief Query the number of Xen vCPUs allocated for a domain.
 *
 * @param domid Domain to query.
 *
 * @return Number of vCPUs on success.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_nr_vcpus(domid_t domid);

/**
 * @brief Set the current level of a guest IRQ line.
 *
 * @param domid Target domain identifier.
 * @param irq Guest IRQ number.
 * @param level Set to non-zero to assert the IRQ, or zero to deassert it.
 *
 * @retval 0 If the IRQ level was updated successfully.
 * @retval -errno Negative error code returned by the hypercall.
 */
int dmop_set_irq_level(domid_t domid, uint32_t irq, uint8_t level);

/** @} */

#endif /* ZEPHYR_XEN_DMOP_H_ */
