/*
 * Copyright (c) 2023 Bjarki Arge Andreasen
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Wakeup source identifiers for the Atmel SAM Supply Controller (SUPC)
 * @ingroup dt_atmel_sam_supc
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_ATMEL_SAM_SUPC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_ATMEL_SAM_SUPC_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_atmel_sam_supc Atmel SAM SUPC wakeup sources
 * @brief Wakeup source identifiers for the Atmel SAM Supply Controller (SUPC)
 * @ingroup devicetree-power
 *
 * The SUPC can wake the device from a low-power state using dedicated peripherals as
 * wakeup sources. Use these identifiers as the cell of the @c wakeup-source-id
 * phandle-array property of the peripheral node, pointing at the @c atmel,sam-supc
 * node:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/atmel_sam_supc.h>
 *
 * &rtc {
 *         wakeup-source-id = <&supc SUPC_WAKEUP_SOURCE_RTC>;
 *         wakeup-source;
 * };
 * @endcode
 *
 * @{
 */

/** Force wake-up pin (FWUP) */
#define SUPC_WAKEUP_SOURCE_FWUP 0
/** Supply monitor (SM) */
#define SUPC_WAKEUP_SOURCE_SM   1
/** Real-time timer (RTT) */
#define SUPC_WAKEUP_SOURCE_RTT  2
/** Real-time clock (RTC) */
#define SUPC_WAKEUP_SOURCE_RTC  3

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_ATMEL_SAM_SUPC_H_ */
