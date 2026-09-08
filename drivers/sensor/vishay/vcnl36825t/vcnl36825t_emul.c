/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT vishay_vcnl36825t

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Document Number 80235, Rev. 1.4, 28-Nov-2024, tables 1 to 13 (command code and register
 * description): https://www.vishay.com/docs/80235/vcnl36825t.pdf
 *
 * Every command code addresses a 16-bit word transferred data byte low first (fig. 9). Table 1
 * names the two halves of each word, for example PS_CONF1_L and PS_CONF1_H at 0x00.
 */
enum {
	PS_CONF1 = 0x00,
	PS_CONF2 = 0x03,
	PS_CONF3 = 0x04,
	PS_THDL = 0x05,
	PS_THDH = 0x06,
	PS_CANC = 0x07,
	PS_CONF4 = 0x08,
	PS_DATA = 0xf8,
	INT_FLAG = 0xf9,
	ID = 0xfa,
	PS_AC_DATA = 0xfb,
};

static const struct emul_sensor_reg registers[] = {
	/* PS_CONF1_L bit 0 reads 1 and must stay 1 (table 2). */
	{PS_CONF1, "PS_CONF1", .reset = 0x0001},
	/* PS_CONF2_L bit 0 is PS_ST, 1 = PS stop (table 4). */
	{PS_CONF2, "PS_CONF2", .reset = 0x0001},
	/* PS_TRIG returns to 0 once the forced cycle has been output (table 6). */
	{PS_CONF3, "PS_CONF3", .convert_on_write = BIT(5), .self_clear = BIT(5)},
	/* Thresholds and cancellation hold 12 bits, 0x000 to 0xfff (tables 8 to 10). */
	{PS_THDL, "PS_THDL", .write_mask = 0x0fff},
	{PS_THDH, "PS_THDH", .write_mask = 0x0fff},
	{PS_CANC, "PS_CANC", .write_mask = 0x0fff},
	{PS_CONF4, "PS_CONF4"},
	{PS_DATA, "PS_DATA", EMUL_SENSOR_REG_RO},
	/* PS_ACFLAG, PS_SPFLAG, PS_IF_CLOSE and PS_IF_AWAY clear when INT_FLAG is read. */
	{INT_FLAG, "INT_FLAG", EMUL_SENSOR_REG_RO,
	 .clear_on_read = GENMASK(13, 12) | GENMASK(9, 8)},
	/* ID_L is 0x26, ID_H holds the slave address 0x60 encoding and version code 0. */
	{ID, "ID", EMUL_SENSOR_REG_RO, .reset = 0x0026},
	{PS_AC_DATA, "PS_AC_DATA", EMUL_SENSOR_REG_RO},
};

static const struct emul_sensor_channel channels[] = {
	/*
	 * PS output is raw ADC counts and SENSOR_CHAN_PROX is adimensional, so one LSB is one
	 * count. PS_CONF3_H bit 12 (PS_HD) selects 12-bit or 16-bit output (table 7).
	 * PS_CONF1_L bit 1 (PS_ON) enables the bias circuit; PS_CAL and PS_CONF1_H bit 9 are
	 * initialization requirements with no stated effect on the output.
	 */
	{.chan = SENSOR_CHAN_PROX, .reg = PS_DATA, .whole_word = true, .lsb = 1.0,
	 .select = {.reg = PS_CONF3, .mask = BIT(12)},
	 .variants = {{.bits = 12}, {.bits = 16}},
	 .disabled = {.reg = PS_CONF1, .mask = BIT(1), .value = 0}},
};

/* Every access carries the command code, and no pointer auto-increment is specified (fig. 9). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .fixed_pointer = true,
	.disabled = {.reg = PS_CONF2, .mask = BIT(0), .value = BIT(0)});
