/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT vishay_vcnl4040

#include <zephyr/drivers/emul_sensor_regmap.h>

/* VCNL4040 datasheet 84274 Rev. 1.7, table 1; 16-bit registers, low byte first */
static const struct emul_sensor_reg vcnl4040_regs[] = {
	{0x00, "ALS_CONF", .reset = 0x0001},
	{0x01, "ALS_THDH"},
	{0x02, "ALS_THDL"},
	{0x03, "PS_CONF1_2", .reset = 0x0001},
	{0x04, "PS_CONF3_MS"},
	{0x05, "PS_CANC"},
	{0x06, "PS_THDL"},
	{0x07, "PS_THDH"},
	{0x08, "PS_DATA", EMUL_SENSOR_REG_RO},
	{0x09, "ALS_DATA", EMUL_SENSOR_REG_RO},
	{0x0A, "WHITE_DATA", EMUL_SENSOR_REG_RO},
	{0x0B, "INT_FLAG", EMUL_SENSOR_REG_RO},
	{0x0C, "ID", EMUL_SENSOR_REG_RO, .reset = 0x0186},
};

static const struct emul_sensor_channel vcnl4040_channels[] = {
	/* ALS_IT selects the integration time and with it the lux per step (table 14) */
	{SENSOR_CHAN_LIGHT, .reg = 0x09, .bits = 16, .select = {0x00, GENMASK(7, 6)},
	 .variants = {{.lsb = 0.1}, {.lsb = 0.05}, {.lsb = 0.025}, {.lsb = 0.0125}}},
	/* PS_HD selects 12 or 16 bit proximity counts */
	{SENSOR_CHAN_PROX, .reg = 0x08, .lsb = 1.0, .select = {0x03, BIT(11)},
	 .variants = {{.bits = 12}, {.bits = 16}}},
};

EMUL_SENSOR_REGMAP_DEFINE(vcnl4040_regs, vcnl4040_channels, .reg_bytes = 2);
