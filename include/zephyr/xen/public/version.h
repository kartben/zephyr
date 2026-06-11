/* SPDX-License-Identifier: MIT */
/******************************************************************************
 * version.h
 *
 * Xen version, type, and compile information.
 *
 * Copyright (c) 2005, Nguyen Anh Quynh <aquynh@gmail.com>
 * Copyright (c) 2005, Keir Fraser <keir@xensource.com>
 *
 * Copyright (c) 2025 EPAM Systems
 */

/**
 * @file
 * @brief Imported Xen version-query ABI definitions.
 * @ingroup xen_public_version
 */
/**
 * @defgroup xen_public_version Xen version ABI
 * @ingroup xen_public_abi
 * @brief Collect the imported Xen version-query operation identifiers and limits.
 * @{
 */

#ifndef __XEN_PUBLIC_VERSION_H__
#define __XEN_PUBLIC_VERSION_H__

/** @cond INTERNAL_HIDDEN */


#include "xen.h"

/* NB. All ops return zero on success, except XENVER_{version,pagesize}
 * XENVER_{version,pagesize,build_id}
 */

/* arg == NULL; returns major:minor (16:16). */
#define XENVER_version      0

/* arg == xen_extraversion_t. */
#define XENVER_extraversion 1
#define XEN_EXTRAVERSION_LEN 16

/** @endcond */

/** @} */

#endif /* __XEN_PUBLIC_VERSION_H__ */
