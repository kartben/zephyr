/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lps25hb_press

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * LPS25HB datasheet DocID027112 Rev 4, section 7 "Register mapping", Table 17.
 * https://www.st.com/resource/en/datasheet/lps25hb.pdf
 * Temperature offset from technical note TN1228, DocID028580 Rev 1, section 3.
 */
enum {
	REF_P_XL = 0x08, REF_P_L = 0x09, REF_P_H = 0x0a, WHO_AM_I = 0x0f, RES_CONF = 0x10,
	CTRL_REG1 = 0x20, CTRL_REG2 = 0x21, CTRL_REG3 = 0x22, CTRL_REG4 = 0x23,
	INTERRUPT_CFG = 0x24, INT_SOURCE = 0x25, STATUS_REG = 0x27, PRESS_OUT_XL = 0x28,
	PRESS_OUT_L = 0x29, PRESS_OUT_H = 0x2a, TEMP_OUT_L = 0x2b, TEMP_OUT_H = 0x2c,
	FIFO_CTRL = 0x2e, FIFO_STATUS = 0x2f, THS_P_L = 0x30, THS_P_H = 0x31, RPDS_L = 0x39,
	RPDS_H = 0x3a,
};

/* STATUS_REG (27h) */
#define P_OR BIT(5)
#define T_OR BIT(4)
#define P_DA BIT(1)
#define T_DA BIT(0)

#define RESERVED(_addr) {_addr, "Reserved", EMUL_SENSOR_REG_RO}

/* Table 17 leaves 0Bh and 0Ch out of both the named and the reserved ranges. */
static const struct emul_sensor_reg registers[] = {
	RESERVED(0x00), RESERVED(0x01), RESERVED(0x02), RESERVED(0x03),
	RESERVED(0x04), RESERVED(0x05), RESERVED(0x06), RESERVED(0x07),
	{REF_P_XL, "REF_P_XL"},
	{REF_P_L, "REF_P_L"},
	{REF_P_H, "REF_P_H"},
	RESERVED(0x0d), RESERVED(0x0e),
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0xbd},
	{RES_CONF, "RES_CONF", .reset = 0x0f, .write_mask = 0x0f},
	RESERVED(0x11), RESERVED(0x12), RESERVED(0x13), RESERVED(0x14),
	RESERVED(0x15), RESERVED(0x16), RESERVED(0x17), RESERVED(0x18),
	RESERVED(0x19), RESERVED(0x1a), RESERVED(0x1b), RESERVED(0x1c),
	RESERVED(0x1d), RESERVED(0x1e), RESERVED(0x1f),
	/* RESET_AZ is self-cleared */
	{CTRL_REG1, "CTRL_REG1", .self_clear = BIT(1)},
	/* BOOT, SWRESET and ONE_SHOT are self-cleared */
	{CTRL_REG2, "CTRL_REG2", .self_clear = BIT(7) | BIT(2) | BIT(0), .reset_on_write = BIT(2),
	 .convert_on_write = BIT(0)},
	{CTRL_REG3, "CTRL_REG3", .write_mask = 0xc3},
	{CTRL_REG4, "CTRL_REG4", .write_mask = 0x0f},
	{INTERRUPT_CFG, "INTERRUPT_CFG", .write_mask = 0x07},
	{INT_SOURCE, "INT_SOURCE", EMUL_SENSOR_REG_RO, .clear_on_read = 0x07},
	RESERVED(0x26),
	{STATUS_REG, "STATUS_REG", EMUL_SENSOR_REG_RO},
	{PRESS_OUT_XL, "PRESS_OUT_XL", EMUL_SENSOR_REG_RO},
	{PRESS_OUT_L, "PRESS_OUT_L", EMUL_SENSOR_REG_RO},
	{PRESS_OUT_H, "PRESS_OUT_H", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = STATUS_REG, .mask = P_OR | P_DA}},
	{TEMP_OUT_L, "TEMP_OUT_L", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_H, "TEMP_OUT_H", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = STATUS_REG, .mask = T_OR | T_DA}},
	RESERVED(0x2d),
	{FIFO_CTRL, "FIFO_CTRL"},
	/* EMPTY_FIFO is set out of reset */
	{FIFO_STATUS, "FIFO_STATUS", EMUL_SENSOR_REG_RO, .reset = 0x20},
	{THS_P_L, "THS_P_L"},
	{THS_P_H, "THS_P_H"},
	RESERVED(0x32), RESERVED(0x33), RESERVED(0x34), RESERVED(0x35),
	RESERVED(0x36), RESERVED(0x37), RESERVED(0x38),
	{RPDS_L, "RPDS_L"},
	{RPDS_H, "RPDS_H"},
};

static const struct emul_sensor_channel channels[] = {
	/* 4096 LSB/hPa (Table 3), 1 hPa is 0.1 kPa; 260 to 1260 hPa operating range */
	{.chan = SENSOR_CHAN_PRESS, .reg = PRESS_OUT_XL, .is_signed = true, .bits = 24,
	 .lsb = 1.0 / 40960, .min = 26.0, .max = 126.0,
	 .ready = {.reg = STATUS_REG, .mask = P_DA}, .overrun = P_OR},
	/*
	 * 480 LSB/degC (Table 3), 42.5 degC at a raw code of 0 (TN1228 section 3). Table 3
	 * gives an operating temperature range but no measured range, and -30 degC is below
	 * what the 16-bit encoding reaches, so the field's own range applies.
	 */
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP_OUT_L, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 480, .offset = 42.5,
	 .ready = {.reg = STATUS_REG, .mask = T_DA}, .overrun = T_OR},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	/* SUB(7) enables address auto-increment, SUB(6:0) is the register address */
	.addr_ignore = BIT(7),
	.disabled = {.reg = CTRL_REG1, .mask = BIT(7), .value = 0},
	.block_update = {.reg = CTRL_REG1, .mask = BIT(2), .value = BIT(2)});
