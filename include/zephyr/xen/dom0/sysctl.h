/*
 * Copyright (c) 2025 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Xen Dom0 system control operations.
 * @ingroup xen_dom0_sysctl
 */

#ifndef __XEN_DOM0_SYSCTL_H__
#define __XEN_DOM0_SYSCTL_H__

#include <zephyr/xen/generic.h>
#include <zephyr/xen/public/sysctl.h>
#include <zephyr/xen/public/xen.h>

/**
 * @defgroup xen_dom0 Xen Dom0 control
 * @ingroup xen_support
 * @brief Manage Xen domains and host resources from the privileged domain.
 */

/**
 * @defgroup xen_dom0_sysctl Xen Dom0 sysctl
 * @ingroup xen_dom0
 * @brief Issue Xen sysctl operations that are available only to Dom0.
 * @{
 */

/**
 * @brief Query physical host information from Xen.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param[out] info Buffer that receives the Xen physical host information.
 *
 * @retval 0 If the query completed successfully.
 * @retval -EINVAL If @p info is ``NULL``.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_sysctl_physinfo(struct xen_sysctl_physinfo *info);

/**
 * @brief Enumerate domains known to Xen.
 *
 * @kconfig_dep{CONFIG_XEN_DOM0}
 *
 * @param[out] domaininfo Output array of domain descriptors.
 * @param first First domain identifier to enumerate.
 * @param num Capacity of the @p domaininfo array.
 *
 * @return Number of entries written to @p domaininfo on success.
 * @retval -EINVAL If @p domaininfo is ``NULL`` or @p num is zero.
 * @retval -errno Negative error code returned by the hypercall.
 */
int xen_sysctl_getdomaininfo(struct xen_domctl_getdomaininfo *domaininfo,
     uint16_t first, uint16_t num);

/** @} */

#endif /* __XEN_DOM0_SYSCTL_H__ */
