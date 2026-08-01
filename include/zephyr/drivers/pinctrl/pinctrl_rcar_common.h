/*
 * Copyright (c) 2023 IoT.bzh
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

/**
 * @file
 * @brief Renesas R-Car SoC specific helpers for pinctrl driver.
 * @ingroup pinctrl_interface_ext
 */

#ifndef ZEPHYR_SOC_ARM_RENESAS_RCAR_COMMON_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_RENESAS_RCAR_COMMON_PINCTRL_SOC_H_

#include <zephyr/devicetree.h>
#include <zephyr/dt-bindings/pinctrl/renesas/pinctrl-rcar-common.h>
#include <stdint.h>
#include <zephyr/sys/util_macro.h>

/** Type for R-Car pin function */
struct rcar_pin_func {
	uint8_t bank:5;      /**< IPSR bank number 0 - 18 */
	uint8_t shift:5;     /**< IPSR field bit shift 0 - 28 */
	uint8_t func:4;      /**< Function choice from 0x0 to 0xF */
};

/** Pull-up, pull-down, or bias disable is requested */
#define RCAR_PIN_FLAGS_PULL_SET BIT(0)
/** Performs on/off control of the pull resistors */
#define RCAR_PIN_FLAGS_PUEN     BIT(1)
/** Select pull-up resistor if set pull-down otherwise */
#define RCAR_PIN_FLAGS_PUD      BIT(2)
/** Alternate function for the pin is requested */
#define RCAR_PIN_FLAGS_FUNC_SET BIT(3)
/** Ignore IPSR settings for alternate function pin */
#define RCAR_PIN_FLAGS_FUNC_DUMMY BIT(4)

/** Pull-up enabled */
#define RCAR_PIN_PULL_UP      (RCAR_PIN_FLAGS_PULL_SET | RCAR_PIN_FLAGS_PUEN | RCAR_PIN_FLAGS_PUD)
/** Pull-down enabled */
#define RCAR_PIN_PULL_DOWN    (RCAR_PIN_FLAGS_PULL_SET | RCAR_PIN_FLAGS_PUEN)
/** Pull disabled */
#define RCAR_PIN_PULL_DISABLE  RCAR_PIN_FLAGS_PULL_SET

/** Type for R-Car pin. */
typedef struct pinctrl_soc_pin {
	uint16_t pin;              /**< Pin ID */
	struct rcar_pin_func func; /**< Pin function */
	uint8_t flags;             /**< Flags (bias and pin mode) */
	uint8_t drive_strength;    /**< Drive strength in mA, 0 if not set */
	uint8_t voltage;           /**< I/O voltage (PIN_VOLTAGE_*) */
} pinctrl_soc_pin_t;

/**
 * @brief Utility macro to get the IPSR function encoding from the pin property.
 *
 * @param node_id Node identifier.
 */
#define RCAR_IPSR(node_id) DT_PROP_BY_IDX(node_id, pin, 1)

/**
 * @brief Utility macro to check if an alternate function is selected.
 *
 * @param node_id Node identifier.
 */
#define RCAR_HAS_IPSR(node_id) DT_PROP_HAS_IDX(node_id, pin, 1)

/**
 * @brief Utility macro to initialize R-Car pin function.
 *
 * Offsets are defined in dt-bindings pinctrl-rcar-common.h
 *
 * @param node_id Node identifier.
 */
#define RCAR_PIN_FUNC(node_id)			       \
	{					       \
		((RCAR_IPSR(node_id) >> 10U) & 0x1FU), \
		((RCAR_IPSR(node_id) >> 4U) & 0x1FU),  \
		((RCAR_IPSR(node_id) & 0xFU))	       \
	}

/**
 * @brief Utility macro to check if a pin alternate function is a dummy.
 *
 * A dummy function is used for pins whose IPSR settings must be ignored.
 *
 * @param node_id Node identifier.
 */
#define RCAR_PIN_IS_FUNC_DUMMY(node_id)					       \
	((((RCAR_IPSR(node_id) >> 10U) & 0x1FU) == 0x1F) &&		       \
	 (((RCAR_IPSR(node_id) >> 4U) & 0x1FU) == 0x1F) &&		       \
	 ((RCAR_IPSR(node_id) & 0xFU) == 0xF))

/**
 * @brief Utility macro to initialize R-Car pin flags (bias and pin mode).
 *
 * @param node_id Node identifier.
 */
#define RCAR_PIN_FLAGS(node_id)						       \
	DT_PROP(node_id, bias_pull_up)   * RCAR_PIN_PULL_UP |		       \
	DT_PROP(node_id, bias_pull_down) * RCAR_PIN_PULL_DOWN |		       \
	DT_PROP(node_id, bias_disable)   * RCAR_PIN_PULL_DISABLE |	       \
	RCAR_HAS_IPSR(node_id) * RCAR_PIN_FLAGS_FUNC_SET |		       \
	RCAR_PIN_IS_FUNC_DUMMY(node_id) * RCAR_PIN_FLAGS_FUNC_DUMMY

/**
 * @brief Utility macro to initialize a R-Car pin.
 *
 * @param node_id Node identifier.
 */
#define RCAR_DT_PIN(node_id)						       \
	{								       \
		.pin = DT_PROP_BY_IDX(node_id, pin, 0),			       \
		.func = COND_CODE_1(RCAR_HAS_IPSR(node_id),		       \
				    (RCAR_PIN_FUNC(node_id)), {0}),	       \
		.flags = RCAR_PIN_FLAGS(node_id),			       \
		.drive_strength =					       \
			COND_CODE_1(DT_NODE_HAS_PROP(node_id, drive_strength), \
				    (DT_PROP(node_id, drive_strength)), (0)),  \
		.voltage = COND_CODE_1(DT_NODE_HAS_PROP(node_id,	       \
							power_source),	       \
				       (DT_PROP(node_id, power_source)),       \
				       (PIN_VOLTAGE_NONE)),		       \
	},

/**
 * @brief Utility macro to initialize each pin.
 *
 * @param node_id Node identifier.
 * @param state_prop State property name.
 * @param idx State property entry index.
 */
#define Z_PINCTRL_STATE_PIN_INIT(node_id, state_prop, idx) \
	RCAR_DT_PIN(DT_PROP_BY_IDX(node_id, state_prop, idx))

/**
 * @brief Utility macro to initialize state pins contained in a given property.
 *
 * @param node_id Node identifier.
 * @param prop Property name describing state pins.
 */
#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop) \
	{ DT_FOREACH_PROP_ELEM(node_id, prop, Z_PINCTRL_STATE_PIN_INIT) }

/** Type for a field of a drive strength control register */
struct pfc_drive_reg_field {
	uint16_t pin;   /**< Pin ID */
	uint8_t offset; /**< Field bit offset in the register */
	uint8_t size;   /**< Field size, in bits */
};

/** Type for R-Car drive strength control registers */
struct pfc_drive_reg {
	uint32_t reg;                               /**< Register offset */
	const struct pfc_drive_reg_field fields[8]; /**< Register fields */
};

/** Type for R-Car bias control registers */
struct pfc_bias_reg {
	uint32_t puen;		/**< Pull-enable or pull-up control register */
	uint32_t pud;		/**< Pull-up/down or pull-down control register */
	const uint16_t pins[32];	/**< Pin IDs, one per register bit */
};

/**
 * @brief Utility macro to check if a pin is GPIO capable
 *
 * @param pin
 * @return true if pin is GPIO capable false otherwise
 */
#define RCAR_IS_GP_PIN(pin) (pin < PIN_NOGPSR_START)

#endif /* ZEPHYR_SOC_ARM_RENESAS_RCAR_COMMON_PINCTRL_SOC_H_ */
