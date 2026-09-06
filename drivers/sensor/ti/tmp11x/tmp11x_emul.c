/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_tmp11x

#include <zephyr/drivers/emul_sensor_regmap.h>

/* TMP116 datasheet SBOS740A, section 7.6 */
static const struct emul_sensor_reg tmp116_regs[] = {
	{0x00, "TEMP", EMUL_SENSOR_REG_RO, .reset = 0x8000},
	{0x01, "CFGR", .reset = 0x0220},
	{0x02, "HIGH_LIM", .reset = 0x6000},
	{0x03, "LOW_LIM", .reset = 0x8000},
	{0x04, "EEPROM_UL"},
	{0x05, "EEPROM1"},
	{0x06, "EEPROM2"},
	{0x07, "EEPROM3"},
	{0x08, "EEPROM4"},
	{0x0F, "DEVICE_ID", EMUL_SENSOR_REG_RO, .reset = 0x1116},
};

static const struct emul_sensor_channel tmp116_channels[] = {
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .bits = 16, .lsb = 0.0078125,
	 .min = -55.0, .max = 150.0, .ready = {0x01, BIT(13)}},
};

EMUL_SENSOR_REGMAP_DEFINE(tmp116_regs, tmp116_channels, .reg_bytes = 2, .big_endian = true);
