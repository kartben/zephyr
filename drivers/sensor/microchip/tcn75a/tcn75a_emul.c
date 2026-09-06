/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT microchip_tcn75a

#include <zephyr/drivers/emul_sensor_regmap.h>

/* TCN75A datasheet DS21935D, section 5 */
static const struct emul_sensor_reg tcn75a_regs[] = {
	{0x00, "TA", EMUL_SENSOR_REG_RO},
	{0x01, "CONFIG", .bytes = 1},
	{0x02, "THYST", .reset = 0x4B00},
	{0x03, "TSET", .reset = 0x5000},
};

static const struct emul_sensor_channel tcn75a_channels[] = {
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .min = -40.0, .max = 125.0,
	 .select = {0x01, GENMASK(6, 5)},
	 .variants = {{.bits = 9, .pos = 7, .lsb = 0.5},
		      {.bits = 10, .pos = 6, .lsb = 0.25},
		      {.bits = 11, .pos = 5, .lsb = 0.125},
		      {.bits = 12, .pos = 4, .lsb = 0.0625}}},
};

EMUL_SENSOR_REGMAP_DEFINE(tcn75a_regs, tcn75a_channels, .reg_bytes = 2, .big_endian = true);
