/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_NXP_LPC_LPC8N04_PINCTRL_SOC_H_
#define ZEPHYR_SOC_NXP_LPC_LPC8N04_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/types.h>
#include <soc.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pinctrl_soc_pin_t;

#define Z_PINCTRL_IOCON_MODE(node_id)                                                              \
	(DT_PROP(node_id, bias_bus_hold)    ? IOCON_PIO_MODE(3U)                                   \
	 : DT_PROP(node_id, bias_pull_up)   ? IOCON_PIO_MODE(2U)                                   \
	 : DT_PROP(node_id, bias_pull_down) ? IOCON_PIO_MODE(1U)                                   \
					    : 0U)

#define Z_PINCTRL_IOCON_PINCFG(node_id)                                                            \
	(Z_PINCTRL_IOCON_MODE(node_id) | IOCON_PIO_LPF(DT_PROP(node_id, nxp_low_pass_filter)))

/*
 * Digital pads: FUNC, MODE and LPF. CDRIVE stays 0 (voltage driver), so ILO/IHI,
 * which only apply to the current driver, are not used.
 */
#define Z_PINCTRL_IOCON_D_PIN_MASK (IOCON_PIO_FUNC_MASK | IOCON_PIO_MODE_MASK | IOCON_PIO_LPF_MASK)

/* PIO0_4 and PIO0_5 open-drain I2C pads: FUNC and I2CMODE only. */
#define Z_PINCTRL_IOCON_I_PIN_MASK (IOCON_PIO_FUNC_MASK | IOCON_PIO_I2CMODE_MASK)

/* No analog pads on this SoC. */
#define Z_PINCTRL_IOCON_A_PIN_MASK 0U

#define Z_PINCTRL_STATE_PIN_INIT(group, pin_prop, idx)                                             \
	DT_PROP_BY_IDX(group, pin_prop, idx) | Z_PINCTRL_IOCON_PINCFG(group),

#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,           \
				Z_PINCTRL_STATE_PIN_INIT)}

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_SOC_NXP_LPC_LPC8N04_PINCTRL_SOC_H_ */
