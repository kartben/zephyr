/*
 * Copyright (c) 2021-2024 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Xen grant table helpers.
 * @ingroup xen_grant_tables
 */

#ifndef __XEN_GNTTAB_H__
#define __XEN_GNTTAB_H__

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/xen/public/grant_table.h>

/**
 * @defgroup xen_grant_tables Xen grant tables
 * @ingroup xen_support
 * @brief Share memory with foreign domains and map foreign grants.
 * @{
 */

/**
 * @brief Grant another domain access to a guest frame.
 *
 * @param domid Foreign domain that receives access to the frame.
 * @param gfn Guest frame number to expose.
 * @param readonly Set to ``true`` to grant read-only access.
 *
 * @return Grant reference that identifies the new entry.
 */
grant_ref_t gnttab_grant_access(domid_t domid, unsigned long gfn,
				bool readonly);

/**
 * @brief Release a grant reference created by gnttab_grant_access().
 *
 * @param gref Grant reference to release.
 *
 * @retval 0 If the reference was released.
 */
int gnttab_end_access(grant_ref_t gref);

/**
 * @brief Allocate a page and immediately grant access to it.
 *
 * @param[out] map Buffer that receives the page base address.
 * @param readonly Set to ``true`` to grant read-only access.
 *
 * @return Grant reference on success.
 * @retval -ENOMEM If page allocation fails.
 */
int32_t gnttab_alloc_and_grant(void **map, bool readonly);

/**
 * @brief Allocate pages that can host grant mappings.
 *
 * The returned range is suitable for use as ``host_addr`` in
 * :c:struct:`gnttab_map_grant_ref` entries.
 *
 * @param npages Number of pages to allocate.
 *
 * @return Base address of the allocated range, or ``NULL`` on failure.
 */
void *gnttab_get_pages(unsigned int npages);

/**
 * @brief Release pages allocated by gnttab_get_pages().
 *
 * @param start_addr Base address returned by gnttab_get_pages().
 * @param npages Number of pages to release.
 *
 * @retval 0 If the pages were released successfully.
 * @retval -errno Negative error code reported by the allocator or Xen memory management.
 */
int gnttab_put_pages(void *start_addr, unsigned int npages);

/**
 * @brief Map one or more foreign grant references.
 *
 * @param[in,out] map_ops Array of prepared map operations.
 * @param count Number of entries in @p map_ops.
 *
 * @retval 0 If the hypercall completed.
 * @retval -EFAULT If Xen extended regions are enabled and a host address is outside them.
 * @retval -errno Negative error code returned by the hypercall.
 *
 * Xen also stores a per-entry status code in each ``map_ops[i].status`` field.
 */
int gnttab_map_refs(struct gnttab_map_grant_ref *map_ops, unsigned int count);

/**
 * @brief Unmap one or more foreign grant references.
 *
 * @param[in,out] unmap_ops Array of prepared unmap operations.
 * @param count Number of entries in @p unmap_ops.
 *
 * @return Hypercall return value from ``GNTTABOP_unmap_grant_ref``.
 * @retval -EFAULT If Xen extended regions are enabled and a host address is outside them.
 */
int gnttab_unmap_refs(struct gnttab_unmap_grant_ref *unmap_ops, unsigned int count);

/**
 * @brief Convert a Xen grant-table status into readable text.
 *
 * @param status Negative ``GNTST_*`` status code.
 *
 * @return Pointer to a constant error string.
 */
const char *gnttabop_error(int16_t status);

/** @} */

#endif /* __XEN_GNTTAB_H__ */
