/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Reset cause definitions for the NXP RW PMU
 * @ingroup dt_nxp_rw_pmu
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_NXP_RW_PMU_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_NXP_RW_PMU_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_nxp_rw_pmu NXP RW PMU reset causes
 * @brief Reset cause flags for the NXP RW Power Management Unit (PMU)
 * @ingroup devicetree-power
 *
 * Bit masks of the PMU SYS_RESET registers, used to select which reset causes
 * to enable through the @c reset-causes-en array property of the @c nxp,rw-pmu
 * node:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/nxp_rw_pmu.h>
 *
 * &pmu {
 *         reset-causes-en = <PMU_RESET_CM33_LOCKUP>,
 *                           <PMU_RESET_ITRC>,
 *                           <PMU_RESET_AP_RESET>;
 * };
 * @endcode
 *
 * @{
 */

/** CM33 system soft reset */
#define PMU_RESET_CM33_SOFT_RESET 0x1
/** CM33 core lockup */
#define PMU_RESET_CM33_LOCKUP 0x2
/** Watchdog timer reset */
#define PMU_RESET_WATCHDOG 0x4
/** AP reset request */
#define PMU_RESET_AP_RESET 0x8
/** Code watchdog reset */
#define PMU_RESET_CODE_WATCHDOG 0x10
/** ITRC (Intrusion and Tamper Response Controller) chip reset */
#define PMU_RESET_ITRC 0x20
/** External RESETB pin reset */
#define PMU_RESET_RESETB 0x40
/** Mask covering all reset causes */
#define PMU_RESET_ALL 0x7F

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_NXP_RW_PMU_H_ */
