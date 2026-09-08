/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lis3mdl_magn

#include <zephyr/drivers/emul_sensor_regmap.h>

/* DocID024204 Rev 4, sections 2 and 5-7: https://www.st.com/resource/en/datasheet/lis3mdl.pdf */
enum {
	WHO_AM_I = 0x0f,
	CTRL_REG1 = 0x20, CTRL_REG2 = 0x21, CTRL_REG3 = 0x22, CTRL_REG4 = 0x23,
	CTRL_REG5 = 0x24, STATUS_REG = 0x27,
	OUT_X_L = 0x28, OUT_X_H = 0x29, OUT_Y_L = 0x2a, OUT_Y_H = 0x2b,
	OUT_Z_L = 0x2c, OUT_Z_H = 0x2d, TEMP_OUT_L = 0x2e, TEMP_OUT_H = 0x2f,
	INT_CFG = 0x30, INT_SRC = 0x31, INT_THS_L = 0x32, INT_THS_H = 0x33,
};

/* Table 16 gives no reset value for the reserved addresses and forbids writing them. */
#define RESERVED(_addr) {_addr, "Reserved", EMUL_SENSOR_REG_RO}

static const struct emul_sensor_reg registers[] = {
	RESERVED(0x00), RESERVED(0x01), RESERVED(0x02), RESERVED(0x03), RESERVED(0x04),
	RESERVED(0x05), RESERVED(0x06), RESERVED(0x07), RESERVED(0x08), RESERVED(0x09),
	RESERVED(0x0a), RESERVED(0x0b), RESERVED(0x0c), RESERVED(0x0d), RESERVED(0x0e),
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x3d},
	RESERVED(0x10), RESERVED(0x11), RESERVED(0x12), RESERVED(0x13), RESERVED(0x14),
	RESERVED(0x15), RESERVED(0x16), RESERVED(0x17), RESERVED(0x18), RESERVED(0x19),
	RESERVED(0x1a), RESERVED(0x1b), RESERVED(0x1c), RESERVED(0x1d), RESERVED(0x1e),
	RESERVED(0x1f),
	{CTRL_REG1, "CTRL_REG1", .reset = 0x10, .write_mask = 0xff},
	/* SOFT_RST resets the configuration and user registers. */
	{CTRL_REG2, "CTRL_REG2", .write_mask = 0x6c, .reset_on_write = BIT(2)},
	{CTRL_REG3, "CTRL_REG3", .reset = 0x03, .write_mask = 0x27},
	{CTRL_REG4, "CTRL_REG4", .write_mask = 0x0e},
	{CTRL_REG5, "CTRL_REG5", .write_mask = 0xc0},
	RESERVED(0x25), RESERVED(0x26),
	{STATUS_REG, "STATUS_REG", EMUL_SENSOR_REG_RO},
	{OUT_X_L, "OUT_X_L", EMUL_SENSOR_REG_RO},
	{OUT_X_H, "OUT_X_H", EMUL_SENSOR_REG_RO},
	{OUT_Y_L, "OUT_Y_L", EMUL_SENSOR_REG_RO},
	{OUT_Y_H, "OUT_Y_H", EMUL_SENSOR_REG_RO},
	{OUT_Z_L, "OUT_Z_L", EMUL_SENSOR_REG_RO},
	{OUT_Z_H, "OUT_Z_H", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_L, "TEMP_OUT_L", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_H, "TEMP_OUT_H", EMUL_SENSOR_REG_RO},
	{INT_CFG, "INT_CFG", .write_mask = 0xe7},
	{INT_SRC, "INT_SRC", EMUL_SENSOR_REG_RO},
	/*
	 * Table 16 types both as read only, but section 7.14 describes a threshold to program.
	 * Kept writable: a read-only row would drop a driver's writes instead of failing them.
	 */
	{INT_THS_L, "INT_THS_L", .write_mask = 0xff},
	{INT_THS_H, "INT_THS_H", .write_mask = 0x7f},
};

/*
 * 16-bit two's complement, low byte at the lower address (BLE = 0). CTRL_REG2.FS<1:0> selects
 * +/-4, 8, 12 or 16 gauss at 6842, 3421, 2281 or 1711 LSB/gauss (Table 3, Table 25).
 * STATUS_REG carries a per-axis and a combined flag; both are raised by the axis that samples.
 */
#define MAGN(_chan, _reg, _da, _ovr)                                                               \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                        \
	 .select = {CTRL_REG2, GENMASK(6, 5)},                                                     \
	 .variants = {{.lsb = 1.0 / 6842, .min = -4, .max = 4},                                    \
		      {.lsb = 1.0 / 3421, .min = -8, .max = 8},                                    \
		      {.lsb = 1.0 / 2281, .min = -12, .max = 12},                                  \
		      {.lsb = 1.0 / 1711, .min = -16, .max = 16}},                                 \
	 .ready = {STATUS_REG, BIT(3) | _da}, .overrun = BIT(7) | _ovr}

static const struct emul_sensor_channel channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, OUT_X_L, BIT(0), BIT(4)),
	MAGN(SENSOR_CHAN_MAGN_Y, OUT_Y_L, BIT(1), BIT(5)),
	MAGN(SENSOR_CHAN_MAGN_Z, OUT_Z_L, BIT(2), BIT(6)),
	/* 8 LSB/degC (Table 4); no zero-temperature code is given, so the offset is 0. */
	{SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT_L, .is_signed = true, .bits = 16, .lsb = 0.125,
	 .min = -40, .max = 85,
	 .disabled = {.reg = CTRL_REG1, .mask = BIT(7), .value = 0}},
};

/* MD<1:0> = 01 starts one conversion; 10 and 11 are both power-down (Table 28). */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == CTRL_REG3 && (data->regs[reg] & GENMASK(1, 0)) == 0x01) {
		emul_sensor_regmap_convert(target);
	}
}

/* The sub-address MSb enables auto-increment, the 7 LSb are the address (section 5.1.1). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.addr_ignore = BIT(7),
	.disabled = {.reg = CTRL_REG3, .mask = BIT(1), .value = BIT(1)},
	.block_update = {.reg = CTRL_REG5, .mask = BIT(6), .value = BIT(6)},
	.write = write);
