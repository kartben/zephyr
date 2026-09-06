/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ams_as5600

#include <zephyr/drivers/emul_sensor_regmap.h>

/* AS5600 datasheet DS000365 v1-06, Register Description, figure 21; 12-bit values MSB first */
static const struct emul_sensor_reg as5600_regs[] = {
	{0x00, "ZMCO", EMUL_SENSOR_REG_RO},
	{0x01, "ZPOS_H"},
	{0x02, "ZPOS_L"},
	{0x03, "MPOS_H"},
	{0x04, "MPOS_L"},
	{0x05, "MANG_H"},
	{0x06, "MANG_L"},
	{0x07, "CONF_H"},
	{0x08, "CONF_L"},
	/* MD set: a magnet is present in front of the emulated sensor */
	{0x0B, "STATUS", EMUL_SENSOR_REG_RO, .reset = BIT(5)},
	{0x0C, "RAW_ANGLE_H", EMUL_SENSOR_REG_RO},
	{0x0D, "RAW_ANGLE_L", EMUL_SENSOR_REG_RO},
	{0x0E, "ANGLE_H", EMUL_SENSOR_REG_RO},
	{0x0F, "ANGLE_L", EMUL_SENSOR_REG_RO},
	{0x1A, "AGC", EMUL_SENSOR_REG_RO},
	{0x1B, "MAGNITUDE_H", EMUL_SENSOR_REG_RO},
	{0x1C, "MAGNITUDE_L", EMUL_SENSOR_REG_RO},
	/* Burn_Angle (0x80) and Burn_Setting (0x40) are one-shot commands */
	{0xFF, "BURN", .self_clear = BIT(7) | BIT(6)},
};

static const struct emul_sensor_channel as5600_channels[] = {
	/*
	 * RAW ANGLE is the unscaled and unmodified angle, 4096 LSB per full turn, and sets
	 * STATUS.MD. ANGLE (0x0E) is derived from it through ZPOS/MPOS/MANG and is not driven;
	 * with those at their reset value of 0 it would equal RAW ANGLE.
	 */
	{SENSOR_CHAN_ROTATION, .reg = 0x0C, .bits = 12, .lsb = 360.0 / 4096, .min = 0.0,
	 .max = 360.0 - 360.0 / 4096, .ready = {0x0B, BIT(5)}},
};

EMUL_SENSOR_REGMAP_DEFINE(as5600_regs, as5600_channels, .big_endian = true);
