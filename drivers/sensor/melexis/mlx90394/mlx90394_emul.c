/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT melexis_mlx90394

#include <zephyr/drivers/emul_sensor_regmap.h>

/* MLX90394 datasheet 3901090394 rev 004, section 5.5 (table 23); 16-bit data low byte first */
static const struct emul_sensor_reg mlx90394_regs[] = {
	/* RT set after reset */
	{0x00, "STAT1", EMUL_SENSOR_REG_RO, .reset = 0x08},
	{0x01, "X_L", EMUL_SENSOR_REG_RO},
	{0x02, "X_H", EMUL_SENSOR_REG_RO},
	{0x03, "Y_L", EMUL_SENSOR_REG_RO},
	{0x04, "Y_H", EMUL_SENSOR_REG_RO},
	{0x05, "Z_L", EMUL_SENSOR_REG_RO},
	{0x06, "Z_H", EMUL_SENSOR_REG_RO},
	{0x07, "STAT2", EMUL_SENSOR_REG_RO},
	{0x08, "T_L", EMUL_SENSOR_REG_RO},
	{0x09, "T_H", EMUL_SENSOR_REG_RO},
	{0x0A, "CID", EMUL_SENSOR_REG_RO, .reset = 0x94},
	{0x0B, "DID", EMUL_SENSOR_REG_RO, .reset = 0xAA},
	{0x0C, "RESERVED_0C", EMUL_SENSOR_REG_RO},
	{0x0D, "RESERVED_0D", EMUL_SENSOR_REG_RO},
	/* X_EN, Y_EN and Z_EN set, MODE power-down */
	{0x0E, "CTRL1", .reset = 0x70},
	/* INTB_SCL_B set, CONFIG 0 */
	{0x0F, "CTRL2", .reset = 0x08},
	{0x10, "RESERVED_10", EMUL_SENSOR_REG_RO},
	/* Writing 0x06 resets the device */
	{0x11, "RST", .self_clear = 0x06},
	{0x12, "RESERVED_12", EMUL_SENSOR_REG_RO},
	{0x13, "RESERVED_13", EMUL_SENSOR_REG_RO},
	/* OSR_HALL, OSR_TEMP, DIG_FILT_HALL_XY 4, DIG_FILT_TEMP 1 */
	{0x14, "CTRL3", .reset = 0xE1},
	/* Reserved bits 7 and 4 set, DIG_FILT_HALL_Z 5 */
	{0x15, "CTRL4", .reset = 0x95},
	{0x58, "X_THR_L"},
	{0x59, "X_THR_H"},
	{0x5A, "Y_THR_L"},
	{0x5B, "Y_THR_H"},
	{0x5C, "Z_THR_L"},
	{0x5D, "Z_THR_H"},
};

/*
 * CTRL2.CONFIG selects the range: 0, 1 and 3 give +/-50 mT at 1.5 uT/LSB, 2 gives +/-5 mT at
 * 0.15 uT/LSB (tables 7, 12 and 22). 1 uT = 0.01 gauss. Sets STAT1.DRDY.
 */
#define MAGN(_chan, _reg)                                                                          \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .select = {0x0F, GENMASK(7, 6)},       \
	 .variants = {{.lsb = 0.015}, {.lsb = 0.015}, {.lsb = 0.0015}, {.lsb = 0.015}},            \
	 .ready = {0x00, BIT(0)}}

static const struct emul_sensor_channel mlx90394_channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, 0x01),
	MAGN(SENSOR_CHAN_MAGN_Y, 0x03),
	MAGN(SENSOR_CHAN_MAGN_Z, 0x05),
	/* 50 LSB/degC, 0 LSB at 0 degC (table 9) */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x08, .is_signed = true, .bits = 16, .lsb = 0.02,
	 .min = -40.0, .max = 105.0, .ready = {0x00, BIT(0)}},
};

EMUL_SENSOR_REGMAP_DEFINE(mlx90394_regs, mlx90394_channels);
