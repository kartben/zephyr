/*
 * Copyright 2025 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Power domain definitions for NXP i.MX943 SoC
 * @ingroup imx943_power_domains
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX943_POWER_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX943_POWER_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup imx943_power_domains NXP i.MX943 Power Domains
 * @brief Power domain slice indices for i.MX943 SoC
 * @ingroup devicetree-power
 *
 * These indices identify the MIX power domain slices managed by the System
 * Manager firmware, and are used as domain IDs with the SCMI power domain
 * protocol.
 *
 * @{
 */

/** Analog power domain */
#define IMX943_PD_ANA       0
/** Always-on power domain */
#define IMX943_PD_AON       1
/** BBSM (Battery-Backed Security Module) power domain */
#define IMX943_PD_BBSM      2
/** M7 Core 1 power domain */
#define IMX943_PD_M7_1      3
/** CCM/SRC/GPC power domain */
#define IMX943_PD_CCMSRCGPC 4
/** A55 Core 0 power domain */
#define IMX943_PD_A55C0     5
/** A55 Core 1 power domain */
#define IMX943_PD_A55C1     6
/** A55 Core 2 power domain */
#define IMX943_PD_A55C2     7
/** A55 Core 3 power domain */
#define IMX943_PD_A55C3     8
/** A55 Platform power domain */
#define IMX943_PD_A55P      9
/** DDR subsystem power domain */
#define IMX943_PD_DDR       10
/** Display subsystem power domain */
#define IMX943_PD_DISPLAY   11
/** M7 Core 0 power domain */
#define IMX943_PD_M7_0      12
/** HSIO Top power domain */
#define IMX943_PD_HSIO_TOP  13
/** HSIO Wakeup-AON power domain */
#define IMX943_PD_HSIO_WAON 14
/** NETC (Network Controller) power domain */
#define IMX943_PD_NETC      15
/** NOC (Network-on-Chip) power domain */
#define IMX943_PD_NOC       16
/** NPU (Neural Processing Unit) power domain */
#define IMX943_PD_NPU       17
/** Wakeup domain power domain */
#define IMX943_PD_WAKEUP    18

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX943_POWER_H_ */
