/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT st_lps22hb_press

#include <zephyr/drivers/emul_sensor_regmap.h>

/* LPS22HB datasheet DocID027083 Rev 6, table 16 */
static const struct emul_sensor_reg lps22hb_regs[] = {
	{0x0B, "INTERRUPT_CFG"},
	{0x0C, "THS_P_L"},
	{0x0D, "THS_P_H"},
	{0x0F, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0xB1},
	{0x10, "CTRL_REG1"},
	/* BOOT, SWRESET and ONE_SHOT self-clear */
	{0x11, "CTRL_REG2", .reset = 0x10, .self_clear = BIT(7) | BIT(2) | BIT(0)},
	{0x12, "CTRL_REG3"},
	{0x14, "FIFO_CTRL"},
	{0x15, "REF_P_XL"},
	{0x16, "REF_P_L"},
	{0x17, "REF_P_H"},
	{0x18, "RPDS_L"},
	{0x19, "RPDS_H"},
	{0x1A, "RES_CONF"},
	{0x25, "INT_SOURCE", EMUL_SENSOR_REG_RO},
	{0x26, "FIFO_STATUS", EMUL_SENSOR_REG_RO},
	{0x27, "STATUS", EMUL_SENSOR_REG_RO},
	{0x28, "PRESS_OUT_XL", EMUL_SENSOR_REG_RO},
	{0x29, "PRESS_OUT_L", EMUL_SENSOR_REG_RO},
	{0x2A, "PRESS_OUT_H", EMUL_SENSOR_REG_RO},
	{0x2B, "TEMP_OUT_L", EMUL_SENSOR_REG_RO},
	{0x2C, "TEMP_OUT_H", EMUL_SENSOR_REG_RO},
	{0x33, "LPFP_RES", EMUL_SENSOR_REG_RO},
};

static const struct emul_sensor_channel lps22hb_channels[] = {
	/* 4096 LSB/hPa, 260 to 1260 hPa, sets STATUS.P_DA */
	{SENSOR_CHAN_PRESS, .reg = 0x28, .is_signed = true, .bits = 24, .lsb = 0.1 / 4096,
	 .min = 26.0, .max = 126.0, .ready = {0x27, BIT(0)}},
	/* 100 LSB/degC, sets STATUS.T_DA */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x2B, .is_signed = true, .bits = 16, .lsb = 0.01,
	 .min = -40.0, .max = 85.0, .ready = {0x27, BIT(1)}},
};

EMUL_SENSOR_REGMAP_DEFINE(lps22hb_regs, lps22hb_channels);
