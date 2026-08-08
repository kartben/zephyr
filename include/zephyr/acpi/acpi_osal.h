/*
 * Copyright (c) 2023 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Header file selecting the OS abstraction layer for the ACPICA library.
 * @ingroup internal_api
 */

#ifndef ZEPHYR_INCLUDE_ACPI_ACPI_OSAL_H_
#define ZEPHYR_INCLUDE_ACPI_ACPI_OSAL_H_

#if defined(CONFIG_X86 || CONFIG_X86_64)
#include <zephyr/acpi/x86_acpi_osal.h>
#else
#error "Currently only x86 Architecture support ACPI !!"
#endif

#endif /* ZEPHYR_INCLUDE_ACPI_ACPI_OSAL_H_ */
