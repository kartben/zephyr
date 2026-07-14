/*
 * Copyright (c) 2026 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief I/O cell definitions for STM32N6 devices
 * @ingroup dt_stm32n6_iocell
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32N6_IOCELL_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32N6_IOCELL_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_stm32n6_iocell STM32N6 I/O cell domains
 * @brief I/O power domain identifiers for the STM32N6 I/O cell controller
 * @ingroup devicetree-power
 *
 * Use these values as the @c domain property of @c st,stm32-iocell child
 * nodes to select the I/O power domain a cell manages:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/stm32n6_iocell.h>
 *
 * iocell: iocell {
 *         compatible = "st,stm32-iocell";
 *
 *         vddio2 {
 *                 domain = <STM32_DT_IOCELL_VDDIO2>;
 *         };
 * };
 * @endcode
 *
 * @{
 */

#define STM32_DT_IOCELL_VDDIO2 (0) /**< I/O domain powered by VDDIO2 supply */
#define STM32_DT_IOCELL_VDDIO3 (1) /**< I/O domain powered by VDDIO3 supply */
#define STM32_DT_IOCELL_VDDIO4 (2) /**< I/O domain powered by VDDIO4 supply */
#define STM32_DT_IOCELL_VDDIO5 (3) /**< I/O domain powered by VDDIO5 supply */
#define STM32_DT_IOCELL_VDD    (4) /**< I/O domain powered by VDD supply */

/**
 * @brief Check that @a domain is a valid I/O domain identifier
 *
 * @param domain I/O domain identifier
 * @return true if @a domain is a valid I/O domain, false otherwise
 */
#define STM32_DT_IOCELL_DOMAIN_VALID(domain) (((domain) >= 0) && ((domain) <= 4))

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32N6_IOCELL_H_ */
