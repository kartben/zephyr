/*
 * Copyright (c) 2026 CodeWrights GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief I/O cell definitions for STM32H5 devices
 * @ingroup dt_stm32h5_iocell
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H5_IOCELL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H5_IOCELL_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_stm32h5_iocell STM32H5 I/O cell domains
 * @brief I/O power domain identifiers for the STM32H5 I/O cell controller
 * @ingroup devicetree-power
 *
 * Use these values as the @c domain property of @c st,stm32h5-iocell child
 * nodes to select the I/O power domain a cell manages:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/stm32h5_iocell.h>
 *
 * iocell: iocell {
 *         compatible = "st,stm32h5-iocell";
 *
 *         vddio2 {
 *                 domain = <STM32_DT_IOCELL_VDDIO2>;
 *         };
 * };
 * @endcode
 *
 * @{
 */

#define STM32_DT_IOCELL_VDDIO  (0) /**< I/O domain powered by VDD supply */
#define STM32_DT_IOCELL_VDDIO2 (1) /**< I/O domain powered by VDDIO2 supply */

/**
 * @brief Check that @a domain is a valid I/O domain identifier
 *
 * @param domain I/O domain identifier
 * @return true if @a domain is a valid I/O domain, false otherwise
 */
#define STM32_DT_IOCELL_DOMAIN_VALID(domain) (((domain) >= 0) && ((domain) <= 1))

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32H5_IOCELL_H_ */
