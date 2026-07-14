/*
 * Copyright (c) 2026 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief I/O cell definitions for STM32H7R/S devices
 * @ingroup dt_stm32h7rs_iocell
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H7RS_IOCELL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H7RS_IOCELL_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_stm32h7rs_iocell STM32H7R/S I/O cell domains
 * @brief I/O power domain identifiers for the STM32H7R/S I/O cell controller
 * @ingroup devicetree-power
 *
 * Use these values as the @c domain property of @c st,stm32-iocell child
 * nodes to select the I/O power domain a cell manages:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/stm32h7rs_iocell.h>
 *
 * iocell: iocell {
 *         compatible = "st,stm32-iocell";
 *
 *         vddxspi1 {
 *                 domain = <STM32_DT_IOCELL_XSPI1>;
 *         };
 * };
 * @endcode
 *
 * @{
 */

#define STM32_DT_IOCELL_VDDIO (0) /**< I/O domain powered by VDD supply */
#define STM32_DT_IOCELL_XSPI1 (1) /**< I/O domain powered by VDDXSPI1 supply */
#define STM32_DT_IOCELL_XSPI2 (2) /**< I/O domain powered by VDDXSPI2 supply */

/**
 * @brief Check that @a domain is a valid I/O domain identifier
 *
 * @param domain I/O domain identifier
 * @return true if @a domain is a valid I/O domain, false otherwise
 */
#define STM32_DT_IOCELL_DOMAIN_VALID(domain) (((domain) >= 0) && ((domain) <= 2))

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H7RS_IOCELL_H_ */
