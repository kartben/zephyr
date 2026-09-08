/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_tmp1075

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SBOS854F, revision F (June 2024), section 7.5 register map:
 * https://www.ti.com/lit/ds/symlink/tmp1075.pdf
 * Models the TMP1075 orderables; the TMP1075N resets CFGR to 60A0h and has no DIEID.
 */
enum { TEMP = 0x00, CFGR = 0x01, LLIM = 0x02, HLIM = 0x03, DIEID = 0x0f };

static const struct emul_sensor_reg registers[] = {
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO},
	/* Unused bits 7:0 reset to FFh; OS reads back 0 and converts only from shutdown */
	{CFGR, "CFGR", .reset = 0x00ff, .write_mask = 0xffff, .self_clear = BIT(15),
		 .convert_on_write = BIT(15), .requires_standby = true},
	/* 4B00h is 75 degC and 5000h is 80 degC; tables 7-9 and 7-10 type bits 3:0 R/W */
	{LLIM, "LLIM", .reset = 0x4b00, .write_mask = 0xffff},
	{HLIM, "HLIM", .reset = 0x5000, .write_mask = 0xffff},
	/* Table 7-11 types DID R/W, table 7-5 and figure 7-16 make the register read only */
	{DIEID, "DIEID", EMUL_SENSOR_REG_RO, .reset = 0x7500},
};

static const struct emul_sensor_channel channels[] = {
	/* 12-bit two's complement in bits 15:4, 0.0625 degC/LSB, bits 3:0 read zero */
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .is_signed = true, .whole_word = true,
	 .bits = 12, .pos = 4, .lsb = 0.0625,
	 .min = -55, .max = 125},
};

/* Table 7-4: only P[3:0] of the pointer byte select a register */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true, .addr_ignore = 0xf0,
	.disabled = {.reg = CFGR, .mask = BIT(8), .value = BIT(8)});
