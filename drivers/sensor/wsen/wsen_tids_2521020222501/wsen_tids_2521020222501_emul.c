/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT we_wsen_tids_2521020222501

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * WSEN-TIDS 2521020222501 user manual version 1.3, April 22 2026, sections 11 and 12
 * https://www.we-online.com/components/products/manual/Manual-um-wsen-tids-2521020222501%20(rev1.3).pdf
 */
enum {
	DEVICE_ID = 0x01,
	T_H_LIMIT = 0x02,
	T_L_LIMIT = 0x03,
	CTRL = 0x04,
	STATUS = 0x05,
	DATA_T_L = 0x06,
	DATA_T_H = 0x07,
	SOFT_RESET = 0x0c,
};

static const struct emul_sensor_reg registers[] = {
	{DEVICE_ID, "DEVICE_ID", EMUL_SENSOR_REG_RO, .reset = 0xa0},
	{T_H_LIMIT, "T_H_LIMIT", .write_mask = 0xff},
	{T_L_LIMIT, "T_L_LIMIT", .write_mask = 0xff},
	/*
	 * BDU(6), AVG[1:0](5:4), IF_ADD_INC(3), FREERUN(2), ONE_SHOT(0); bits 7 and 1 read 0.
	 * Single conversion mode is entered from power-down only (section 8.7).
	 */
	{CTRL, "CTRL", .write_mask = GENMASK(6, 2) | BIT(0), .convert_on_write = BIT(0),
		.requires_standby = true},
	/* UNDER_TLL(2) and OVER_THL(1) clear on read; BUSY(0) is set only while converting. */
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO, .clear_on_read = GENMASK(2, 1)},
	{DATA_T_L, "DATA_T_L", EMUL_SENSOR_REG_RO},
	{DATA_T_H, "DATA_T_H", EMUL_SENSOR_REG_RO},
	/* SWRESET(1) holds the digital blocks in reset until the host writes it back to 0. */
	{SOFT_RESET, "SOFT_RESET", .write_mask = BIT(1), .reset_on_write = BIT(1)},
};

static const struct emul_sensor_channel channels[] = {
	/* DATA_T_L is the low byte of a 16-bit word, 0.01 degC/digit (SENT, table 6) */
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = DATA_T_L, .is_signed = true, .bits = 16,
	 .lsb = 0.01, .min = -40, .max = 125},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.byte_addressed = true,
	.increment = {CTRL, BIT(3)},
	.disabled = {.reg = CTRL, .mask = BIT(2), .value = 0},
	.block_update = {.reg = CTRL, .mask = BIT(6), .value = BIT(6)});
