/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT isil_isl29035

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * FN8371 Rev 3.00, Table 2 "Register Map" and the "Register Description" section:
 * https://www.renesas.com/us/en/document/dst/isl29035-datasheet
 */
enum {
	COMMAND_I = 0x00,
	COMMAND_II = 0x01,
	DATALSB = 0x02,
	DATAMSB = 0x03,
	INT_LT_LSB = 0x04,
	INT_LT_MSB = 0x05,
	INT_HT_LSB = 0x06,
	INT_HT_MSB = 0x07,
	ID = 0x0f,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * B7-B5 are the operation mode, B4-B3 reserved, B2 the interrupt status cleared at the
	 * end of a read of this register, B1-B0 the interrupt persist count. Any mode other than
	 * 000 starts converting.
	 */
	{COMMAND_I, "COMMAND-I", .write_mask = GENMASK(7, 5) | GENMASK(1, 0),
	 .clear_on_read = BIT(2), .convert_on_write = GENMASK(7, 5)},
	/* B7-B4 reserved, B3-B2 the ADC resolution, B1-B0 the full scale Lux range */
	{COMMAND_II, "COMMAND-II", .write_mask = GENMASK(3, 0)},
	{DATALSB, "DATALSB", EMUL_SENSOR_REG_RO},
	{DATAMSB, "DATAMSB", EMUL_SENSOR_REG_RO},
	{INT_LT_LSB, "INT_LT_LSB", .write_mask = 0xff},
	{INT_LT_MSB, "INT_LT_MSB", .write_mask = 0xff},
	{INT_HT_LSB, "INT_HT_LSB", .reset = 0xff, .write_mask = 0xff},
	{INT_HT_MSB, "INT_HT_MSB", .reset = 0xff, .write_mask = 0xff},
	/*
	 * BOUT (B7) powers up set and is cleared by an I2C write; B5-B3 are the read-only device
	 * identification 101; B6 and B2-B0 are reserved.
	 */
	{ID, "ID", .reset = 0xa8, .write_mask = BIT(7)},
};

/*
 * Unsigned ADC count in DATALSB:DATAMSB, low byte first. E = (Range / 2^n) x DATA (EQ. 3), with
 * Range the full scale Lux range selected by COMMAND-II RANGE1:RANGE0 (Table 9) and 2^n = 65536
 * counts at the default RES1:RES0 = 00 (Table 10). Modes 001 and 101 measure ALS, 010 and 110 IR
 * (Table 7), so OP0 alone enables the visible channel and OP1 alone the infrared one. The
 * datasheet gives no Lux calibration for the IR channel, which shares the ADC scaling.
 */
#define ISL29035_CHANNEL(_chan, _op_bit)                                                        \
	{_chan, .reg = DATALSB, .bits = 16,                                                     \
	 .select = {COMMAND_II, GENMASK(1, 0)},                                                 \
	 .variants = {{.lsb = 1000.0 / 65536}, {.lsb = 4000.0 / 65536},                         \
		      {.lsb = 16000.0 / 65536}, {.lsb = 64000.0 / 65536}},                      \
	 .disabled = {COMMAND_I, _op_bit, 0}}

static const struct emul_sensor_channel channels[] = {
	ISL29035_CHANNEL(SENSOR_CHAN_LIGHT, BIT(5)),
	ISL29035_CHANNEL(SENSOR_CHAN_IR, BIT(6)),
};

/*
 * One byte per register, address pointer incremented by burst transfers, and mode 000 powers the
 * device down while retaining the data registers.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = COMMAND_I, .mask = GENMASK(7, 5), .value = 0});
