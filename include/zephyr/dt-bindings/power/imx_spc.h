/*
 * Copyright (c) 2021, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Setpoint definitions for the NXP i.MX Set Point Controller (SPC)
 * @ingroup dt_imx_spc
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PM_IMX_SPC_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PM_IMX_SPC_H_

/**
 * @addtogroup devicetree-power Devicetree power helpers
 * @ingroup devicetree
 */

/**
 * @defgroup dt_imx_spc NXP i.MX SPC setpoints
 * @brief Setpoint definitions for the NXP i.MX Set Point Controller (SPC)
 * @ingroup devicetree-power
 *
 * The SPC uses a series of set points to determine the clock speeds and states
 * of cores, as well as which peripherals to gate clocks to. Higher values
 * correspond to more power saving. See your SoC's datasheet for specifics of
 * what peripherals have their clocks gated at each set point.
 *
 * Each <tt>IMX_SPC_SET_POINT_\<n\>_\<mode\></tt> value combines a set point
 * index (0-15) with the GPC CPU mode to enter, and is meant to be used as the
 * @c substate-id property of a @c zephyr,power-state node:
 *
 * @code{.dts}
 * #include <zephyr/dt-bindings/power/imx_spc.h>
 *
 * idle: set_point_1_wait {
 *         compatible = "zephyr,power-state";
 *         power-state-name = "runtime-idle";
 *         substate-id = <IMX_SPC_SET_POINT_1_WAIT>;
 *         min-residency-us = <100>;
 * };
 * @endcode
 *
 * Set point control is implemented at the SoC level, which decodes the
 * substate ID with @ref IMX_SPC and @ref IMX_GPC_MODE (see pm_state_set()).
 *
 * @{
 */

/** GPC RUN mode: core is active */
#define IMX_GPC_RUN		0x0
/** GPC WAIT mode: core clock is gated until an interrupt occurs */
#define IMX_GPC_WAIT	0x1
/** GPC STOP mode: core and peripheral clocks are stopped */
#define IMX_GPC_STOP	0x2
/** GPC SUSPEND mode: core is powered down */
#define IMX_GPC_SUSPEND	0x3

/** @cond INTERNAL_HIDDEN */

#define IMX_SPC_MASK 0xF0
#define IMX_SPC_SHIFT 4
#define IMX_GPC_MODE_MASK 0xF

/* Set point indices, encoded in the upper nibble of the substate ID */
#define IMX_SPC_0	0x00
#define IMX_SPC_1	0x10
#define IMX_SPC_2	0x20
#define IMX_SPC_3	0x30
#define IMX_SPC_4	0x40
#define IMX_SPC_5	0x50
#define IMX_SPC_6	0x60
#define IMX_SPC_7	0x70
#define IMX_SPC_8	0x80
#define IMX_SPC_9	0x90
#define IMX_SPC_10	0xA0
#define IMX_SPC_11	0xB0
#define IMX_SPC_12	0xC0
#define IMX_SPC_13	0xD0
#define IMX_SPC_14	0xE0
#define IMX_SPC_15	0xF0

/** @endcond */

/**
 * @brief Extract the set point index from a power state substate ID
 *
 * @param x substate ID combining a set point index and a GPC mode
 * @return set point index (0-15)
 */
#define IMX_SPC(x) ((x & IMX_SPC_MASK) >> IMX_SPC_SHIFT)

/**
 * @brief Extract the GPC CPU mode from a power state substate ID
 *
 * @param x substate ID combining a set point index and a GPC mode
 * @return one of the <tt>IMX_GPC_*</tt> CPU modes
 */
#define IMX_GPC_MODE(x) (x & IMX_GPC_MODE_MASK)

/** Set point 0 in RUN mode */
#define IMX_SPC_SET_POINT_0_RUN			(IMX_SPC_0 | IMX_GPC_RUN)
/** Set point 0 in WAIT mode */
#define IMX_SPC_SET_POINT_0_WAIT		(IMX_SPC_0 | IMX_GPC_WAIT)
/** Set point 0 in STOP mode */
#define IMX_SPC_SET_POINT_0_STOP		(IMX_SPC_0 | IMX_GPC_STOP)
/** Set point 0 in SUSPEND mode */
#define IMX_SPC_SET_POINT_0_SUSPEND		(IMX_SPC_0 | IMX_GPC_SUSPEND)
/** Set point 1 in RUN mode */
#define IMX_SPC_SET_POINT_1_RUN			(IMX_SPC_1 | IMX_GPC_RUN)
/** Set point 1 in WAIT mode */
#define IMX_SPC_SET_POINT_1_WAIT		(IMX_SPC_1 | IMX_GPC_WAIT)
/** Set point 1 in STOP mode */
#define IMX_SPC_SET_POINT_1_STOP		(IMX_SPC_1 | IMX_GPC_STOP)
/** Set point 1 in SUSPEND mode */
#define IMX_SPC_SET_POINT_1_SUSPEND		(IMX_SPC_1 | IMX_GPC_SUSPEND)
/** Set point 2 in RUN mode */
#define IMX_SPC_SET_POINT_2_RUN			(IMX_SPC_2 | IMX_GPC_RUN)
/** Set point 2 in WAIT mode */
#define IMX_SPC_SET_POINT_2_WAIT		(IMX_SPC_2 | IMX_GPC_WAIT)
/** Set point 2 in STOP mode */
#define IMX_SPC_SET_POINT_2_STOP		(IMX_SPC_2 | IMX_GPC_STOP)
/** Set point 2 in SUSPEND mode */
#define IMX_SPC_SET_POINT_2_SUSPEND		(IMX_SPC_2 | IMX_GPC_SUSPEND)
/** Set point 3 in RUN mode */
#define IMX_SPC_SET_POINT_3_RUN			(IMX_SPC_3 | IMX_GPC_RUN)
/** Set point 3 in WAIT mode */
#define IMX_SPC_SET_POINT_3_WAIT		(IMX_SPC_3 | IMX_GPC_WAIT)
/** Set point 3 in STOP mode */
#define IMX_SPC_SET_POINT_3_STOP		(IMX_SPC_3 | IMX_GPC_STOP)
/** Set point 3 in SUSPEND mode */
#define IMX_SPC_SET_POINT_3_SUSPEND		(IMX_SPC_3 | IMX_GPC_SUSPEND)
/** Set point 4 in RUN mode */
#define IMX_SPC_SET_POINT_4_RUN			(IMX_SPC_4 | IMX_GPC_RUN)
/** Set point 4 in WAIT mode */
#define IMX_SPC_SET_POINT_4_WAIT		(IMX_SPC_4 | IMX_GPC_WAIT)
/** Set point 4 in STOP mode */
#define IMX_SPC_SET_POINT_4_STOP		(IMX_SPC_4 | IMX_GPC_STOP)
/** Set point 4 in SUSPEND mode */
#define IMX_SPC_SET_POINT_4_SUSPEND		(IMX_SPC_4 | IMX_GPC_SUSPEND)
/** Set point 5 in RUN mode */
#define IMX_SPC_SET_POINT_5_RUN			(IMX_SPC_5 | IMX_GPC_RUN)
/** Set point 5 in WAIT mode */
#define IMX_SPC_SET_POINT_5_WAIT		(IMX_SPC_5 | IMX_GPC_WAIT)
/** Set point 5 in STOP mode */
#define IMX_SPC_SET_POINT_5_STOP		(IMX_SPC_5 | IMX_GPC_STOP)
/** Set point 5 in SUSPEND mode */
#define IMX_SPC_SET_POINT_5_SUSPEND		(IMX_SPC_5 | IMX_GPC_SUSPEND)
/** Set point 6 in RUN mode */
#define IMX_SPC_SET_POINT_6_RUN			(IMX_SPC_6 | IMX_GPC_RUN)
/** Set point 6 in WAIT mode */
#define IMX_SPC_SET_POINT_6_WAIT		(IMX_SPC_6 | IMX_GPC_WAIT)
/** Set point 6 in STOP mode */
#define IMX_SPC_SET_POINT_6_STOP		(IMX_SPC_6 | IMX_GPC_STOP)
/** Set point 6 in SUSPEND mode */
#define IMX_SPC_SET_POINT_6_SUSPEND		(IMX_SPC_6 | IMX_GPC_SUSPEND)
/** Set point 7 in RUN mode */
#define IMX_SPC_SET_POINT_7_RUN			(IMX_SPC_7 | IMX_GPC_RUN)
/** Set point 7 in WAIT mode */
#define IMX_SPC_SET_POINT_7_WAIT		(IMX_SPC_7 | IMX_GPC_WAIT)
/** Set point 7 in STOP mode */
#define IMX_SPC_SET_POINT_7_STOP		(IMX_SPC_7 | IMX_GPC_STOP)
/** Set point 7 in SUSPEND mode */
#define IMX_SPC_SET_POINT_7_SUSPEND		(IMX_SPC_7 | IMX_GPC_SUSPEND)
/** Set point 8 in RUN mode */
#define IMX_SPC_SET_POINT_8_RUN			(IMX_SPC_8 | IMX_GPC_RUN)
/** Set point 8 in WAIT mode */
#define IMX_SPC_SET_POINT_8_WAIT		(IMX_SPC_8 | IMX_GPC_WAIT)
/** Set point 8 in STOP mode */
#define IMX_SPC_SET_POINT_8_STOP		(IMX_SPC_8 | IMX_GPC_STOP)
/** Set point 8 in SUSPEND mode */
#define IMX_SPC_SET_POINT_8_SUSPEND		(IMX_SPC_8 | IMX_GPC_SUSPEND)
/** Set point 9 in RUN mode */
#define IMX_SPC_SET_POINT_9_RUN			(IMX_SPC_9 | IMX_GPC_RUN)
/** Set point 9 in WAIT mode */
#define IMX_SPC_SET_POINT_9_WAIT		(IMX_SPC_9 | IMX_GPC_WAIT)
/** Set point 9 in STOP mode */
#define IMX_SPC_SET_POINT_9_STOP		(IMX_SPC_9 | IMX_GPC_STOP)
/** Set point 9 in SUSPEND mode */
#define IMX_SPC_SET_POINT_9_SUSPEND		(IMX_SPC_9 | IMX_GPC_SUSPEND)
/** Set point 10 in RUN mode */
#define IMX_SPC_SET_POINT_10_RUN		(IMX_SPC_10 | IMX_GPC_RUN)
/** Set point 10 in WAIT mode */
#define IMX_SPC_SET_POINT_10_WAIT		(IMX_SPC_10 | IMX_GPC_WAIT)
/** Set point 10 in STOP mode */
#define IMX_SPC_SET_POINT_10_STOP		(IMX_SPC_10 | IMX_GPC_STOP)
/** Set point 10 in SUSPEND mode */
#define IMX_SPC_SET_POINT_10_SUSPEND	(IMX_SPC_10 | IMX_GPC_SUSPEND)
/** Set point 11 in RUN mode */
#define IMX_SPC_SET_POINT_11_RUN		(IMX_SPC_11 | IMX_GPC_RUN)
/** Set point 11 in WAIT mode */
#define IMX_SPC_SET_POINT_11_WAIT		(IMX_SPC_11 | IMX_GPC_WAIT)
/** Set point 11 in STOP mode */
#define IMX_SPC_SET_POINT_11_STOP		(IMX_SPC_11 | IMX_GPC_STOP)
/** Set point 11 in SUSPEND mode */
#define IMX_SPC_SET_POINT_11_SUSPEND	(IMX_SPC_11 | IMX_GPC_SUSPEND)
/** Set point 12 in RUN mode */
#define IMX_SPC_SET_POINT_12_RUN		(IMX_SPC_12 | IMX_GPC_RUN)
/** Set point 12 in WAIT mode */
#define IMX_SPC_SET_POINT_12_WAIT		(IMX_SPC_12 | IMX_GPC_WAIT)
/** Set point 12 in STOP mode */
#define IMX_SPC_SET_POINT_12_STOP		(IMX_SPC_12 | IMX_GPC_STOP)
/** Set point 12 in SUSPEND mode */
#define IMX_SPC_SET_POINT_12_SUSPEND	(IMX_SPC_12 | IMX_GPC_SUSPEND)
/** Set point 13 in RUN mode */
#define IMX_SPC_SET_POINT_13_RUN		(IMX_SPC_13 | IMX_GPC_RUN)
/** Set point 13 in WAIT mode */
#define IMX_SPC_SET_POINT_13_WAIT		(IMX_SPC_13 | IMX_GPC_WAIT)
/** Set point 13 in STOP mode */
#define IMX_SPC_SET_POINT_13_STOP		(IMX_SPC_13 | IMX_GPC_STOP)
/** Set point 13 in SUSPEND mode */
#define IMX_SPC_SET_POINT_13_SUSPEND	(IMX_SPC_13 | IMX_GPC_SUSPEND)
/** Set point 14 in RUN mode */
#define IMX_SPC_SET_POINT_14_RUN		(IMX_SPC_14 | IMX_GPC_RUN)
/** Set point 14 in WAIT mode */
#define IMX_SPC_SET_POINT_14_WAIT		(IMX_SPC_14 | IMX_GPC_WAIT)
/** Set point 14 in STOP mode */
#define IMX_SPC_SET_POINT_14_STOP		(IMX_SPC_14 | IMX_GPC_STOP)
/** Set point 14 in SUSPEND mode */
#define IMX_SPC_SET_POINT_14_SUSPEND	(IMX_SPC_14 | IMX_GPC_SUSPEND)
/** Set point 15 in RUN mode */
#define IMX_SPC_SET_POINT_15_RUN		(IMX_SPC_15 | IMX_GPC_RUN)
/** Set point 15 in WAIT mode */
#define IMX_SPC_SET_POINT_15_WAIT		(IMX_SPC_15 | IMX_GPC_WAIT)
/** Set point 15 in STOP mode */
#define IMX_SPC_SET_POINT_15_STOP		(IMX_SPC_15 | IMX_GPC_STOP)
/** Set point 15 in SUSPEND mode */
#define IMX_SPC_SET_POINT_15_SUSPEND	(IMX_SPC_15 | IMX_GPC_SUSPEND)

/** @} */

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PM_IMX_SPC_H_ */
