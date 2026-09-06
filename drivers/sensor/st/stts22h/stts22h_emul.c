/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_stts22h

#include <zephyr/drivers/emul_sensor_regmap.h>

/* STTS22H datasheet DS12606 Rev 7, table 12 */
static const struct emul_sensor_reg stts22h_regs[] = {
	{0x01, "WHOAMI", EMUL_SENSOR_REG_RO, .reset = 0xA0},
	{0x02, "TEMP_H_LIMIT"},
	{0x03, "TEMP_L_LIMIT"},
	/* ONE_SHOT starts a single conversion and self-clears */
	{0x04, "CTRL", .self_clear = BIT(0)},
	{0x05, "STATUS", EMUL_SENSOR_REG_RO},
	{0x06, "TEMP_L_OUT", EMUL_SENSOR_REG_RO},
	{0x07, "TEMP_H_OUT", EMUL_SENSOR_REG_RO},
};

static const struct emul_sensor_channel stts22h_channels[] = {
	/* 100 LSB/degC (section 8), low byte first */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x06, .is_signed = true, .bits = 16, .lsb = 0.01,
	 .min = -40.0, .max = 125.0},
};

/* Bit 7 of the register address byte has no meaning (section 5.2) */
EMUL_SENSOR_REGMAP_DEFINE(stts22h_regs, stts22h_channels, .addr_ignore = BIT(7));
