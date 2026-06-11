/*
 * Copyright (c) 2026 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Xen extended region management.
 * @ingroup xen_regions
 */

#ifndef ZEPHYR_INCLUDE_XEN_REGIONS_H_
#define ZEPHYR_INCLUDE_XEN_REGIONS_H_

#include <stdbool.h>
#include <stddef.h>

/**
 * @defgroup xen_regions Xen extended regions
 * @ingroup xen_support
 * @brief Allocate, free, and map buffers from Xen extended memory regions.
 * @{
 */

/**
 * @brief Check whether an address belongs to a configured extended region.
 *
 * @param ptr Address to test.
 *
 * @retval true If @p ptr is inside one of the configured extended regions.
 * @retval false If @p ptr is ``NULL`` or outside all configured extended regions.
 */
bool xen_region_is_addr_extreg(void *ptr);

/**
 * @brief Allocate one or more contiguous pages from an extended region.
 *
 * @param nr_pages Number of Xen pages to allocate.
 *
 * @return Base address of the allocated range, or ``NULL`` if no region can satisfy the request.
 */
void *xen_region_get_pages(size_t nr_pages);

/**
 * @brief Release pages previously allocated from an extended region.
 *
 * @param ptr Base address returned by xen_region_get_pages().
 * @param nr_pages Number of pages to release.
 *
 * @retval 0 If the pages were released successfully.
 * @retval -EIO If @p ptr does not belong to a known extended region.
 * @retval -errno Negative error code returned by the memory-block allocator.
 */
int xen_region_put_pages(void *ptr, size_t nr_pages);

/**
 * @brief Map an extended-region buffer into the kernel address space.
 *
 * @param ptr Base address of the extended-region buffer.
 * @param nr_pages Number of pages to map.
 *
 * @retval 0 If the buffer was mapped successfully.
 * @retval -EFAULT If @p ptr does not belong to an extended region.
 */
int xen_region_map(void *ptr, size_t nr_pages);

/**
 * @brief Unmap an extended-region buffer from the kernel address space.
 *
 * @param ptr Base address of the extended-region buffer.
 * @param nr_pages Number of pages to unmap.
 *
 * @retval 0 If the buffer was unmapped successfully.
 * @retval -EFAULT If @p ptr does not belong to an extended region.
 */
int xen_region_unmap(void *ptr, size_t nr_pages);

/** @} */

#endif /* ZEPHYR_INCLUDE_XEN_REGIONS_H_ */
