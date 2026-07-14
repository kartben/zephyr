/*
 * Copyright 2024 NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Power domain definitions for NXP i.MX95 SoC
 * @ingroup imx95_power_domains
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX95_POWER_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX95_POWER_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup imx95_power_domains NXP i.MX95 Power Domains
 * @brief Power domain slice indices for i.MX95 SoC
 * @ingroup devicetree-power
 *
 * These indices identify the MIX power domain slices managed by the System
 * Manager firmware, and are used as domain IDs with the SCMI power domain
 * protocol.
 *
 * @{
 */

/** Analog power domain */
#define IMX95_PD_ANA       0
/** Always-on power domain */
#define IMX95_PD_AON       1
/** BBSM (Battery-Backed Security Module) power domain */
#define IMX95_PD_BBSM      2
/** Camera subsystem power domain */
#define IMX95_PD_CAMERA    3
/** CCM/SRC/GPC power domain */
#define IMX95_PD_CCMSRCGPC 4
/** A55 Core 0 power domain */
#define IMX95_PD_A55C0     5
/** A55 Core 1 power domain */
#define IMX95_PD_A55C1     6
/** A55 Core 2 power domain */
#define IMX95_PD_A55C2     7
/** A55 Core 3 power domain */
#define IMX95_PD_A55C3     8
/** A55 Core 4 power domain */
#define IMX95_PD_A55C4     9
/** A55 Core 5 power domain */
#define IMX95_PD_A55C5     10
/** A55 Platform power domain */
#define IMX95_PD_A55P      11
/** DDR subsystem power domain */
#define IMX95_PD_DDR       12
/** Display subsystem power domain */
#define IMX95_PD_DISPLAY   13
/** GPU power domain */
#define IMX95_PD_GPU       14
/** HSIO Top power domain */
#define IMX95_PD_HSIO_TOP  15
/** HSIO Wakeup-AON power domain */
#define IMX95_PD_HSIO_WAON 16
/** M7 core power domain */
#define IMX95_PD_M7        17
/** NETC (Network Controller) power domain */
#define IMX95_PD_NETC      18
/** NOC (Network-on-Chip) power domain */
#define IMX95_PD_NOC       19
/** NPU (Neural Processing Unit) power domain */
#define IMX95_PD_NPU       20
/** VPU (Video Processing Unit) power domain */
#define IMX95_PD_VPU       21
/** Wakeup domain power domain */
#define IMX95_PD_WAKEUP    22

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_POWER_IMX95_POWER_H_ */
