/*
 * Copyright (c) 2023 STMicroelectronics
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Wake-up pin source flags for the STM32 PWR controller
 * @ingroup dt_stm32_pwr
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32_PWR_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32_PWR_H_

#include <zephyr/dt-bindings/dt-util.h>

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_stm32_pwr STM32 PWR wake-up pins
 * @brief Wake-up event source selection flags for STM32 PWR wake-up pins
 * @ingroup devicetree-power
 *
 * On series where a wake-up pin can be mapped to several wake-up event
 * sources, these flags select, in the GPIO flags cell of the @c wkup-gpios
 * property of an @c st,stm32-pwr wake-up pin child node, which event source
 * the GPIO is associated with:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/stm32_pwr.h>
 *
 * wkup-pin@1 {
 *         reg = <0x1>;
 *         wkup-gpios = <&gpioa 0 STM32_PWR_WKUP_EVT_SRC_0>;
 * };
 * @endcode
 *
 * @{
 */

/** Wake-up event source 0 */
#define STM32_PWR_WKUP_EVT_SRC_0	BIT(0)
/** Wake-up event source 1 */
#define STM32_PWR_WKUP_EVT_SRC_1	BIT(1)
/** Wake-up event source 2 */
#define STM32_PWR_WKUP_EVT_SRC_2	BIT(2)

/** Flag to use on series where the wake-up event source is fixed (not configurable) */
#define STM32_PWR_WKUP_PIN_NOT_MUXED	STM32_PWR_WKUP_EVT_SRC_0

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_STM32_PWR_H_ */
