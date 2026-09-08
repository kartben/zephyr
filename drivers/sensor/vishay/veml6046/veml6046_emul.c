/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT vishay_veml6046

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Document Number 80173, Rev. 1.3, 12-Mar-2025, "REGISTER INFORMATION", Table 2 through
 * Table 11: https://www.vishay.com/docs/80173/veml6046x00.pdf
 *
 * Bit positions of the G_IF_L/G_IF_H interrupt flags (Table 2 only names INT_FLAG without a
 * bit table) and the RGB_IT/RGB_GAIN/RGB_PDDIV resolution table come from the companion
 * Application Note, Document Number 80410, Rev. 24-Aug-2026, sections 4.5 (Table 12) and 6.1
 * (Table 17, Table 18): https://www.vishay.com/docs/80410/designingveml6046x00.pdf
 */
enum {
	RGB_CONF_0 = 0x00,
	RGB_CONF_1 = 0x01,
	G_THDH_L = 0x04,
	G_THDH_H = 0x05,
	G_THDL_L = 0x06,
	G_THDL_H = 0x07,
	R_DATA_L = 0x10,
	R_DATA_H = 0x11,
	G_DATA_L = 0x12,
	G_DATA_H = 0x13,
	B_DATA_L = 0x14,
	B_DATA_H = 0x15,
	IR_DATA_L = 0x16,
	IR_DATA_H = 0x17,
	VEML6046X00_ID_L = 0x18,
	VEML6046X00_ID_H = 0x19,
	/* Datasheet names both 0x1A and 0x1B "INT_FLAG"; split here for unique identifiers. */
	INT_FLAG_L = 0x1a,
	INT_FLAG_H = 0x1b,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * Bit 0 RGB_ON_0 (1 = shutdown, default), bit 1 G_INT, bit 2 RGB_TRIG (self-clears
	 * after the measurement cycle, Table 3), bit 3 RGB_MODE, bits 6:4 RGB_IT, bit 7
	 * reserved (kept 0).
	 */
	{RGB_CONF_0, "RGB_CONF_0", .reset = 0x01, .write_mask = 0x7f,
	 .self_clear = BIT(2), .convert_on_write = BIT(2)},
	/*
	 * Bit 0 RGB_CAL, bits 2:1 G_PERS, bits 4:3 RGB_GAIN, bit 5 reserved (kept 0), bit 6
	 * RGB_PDDIV, bit 7 RGB_ON_1 (1 = shutdown, default). RGB_ON_0 and RGB_ON_1 must be
	 * written together to start or stop the sensor (Register Addresses, page 7).
	 */
	{RGB_CONF_1, "RGB_CONF_1", .reset = 0x80, .write_mask = 0xdf},
	{G_THDH_L, "G_THDH_L", .write_mask = 0xff},
	{G_THDH_H, "G_THDH_H", .write_mask = 0xff},
	{G_THDL_L, "G_THDL_L", .write_mask = 0xff},
	{G_THDL_H, "G_THDL_H", .write_mask = 0xff},
	{R_DATA_L, "R_DATA_L", EMUL_SENSOR_REG_RO},
	{R_DATA_H, "R_DATA_H", EMUL_SENSOR_REG_RO},
	{G_DATA_L, "G_DATA_L", EMUL_SENSOR_REG_RO},
	{G_DATA_H, "G_DATA_H", EMUL_SENSOR_REG_RO},
	{B_DATA_L, "B_DATA_L", EMUL_SENSOR_REG_RO},
	{B_DATA_H, "B_DATA_H", EMUL_SENSOR_REG_RO},
	{IR_DATA_L, "IR_DATA_L", EMUL_SENSOR_REG_RO},
	{IR_DATA_H, "IR_DATA_H", EMUL_SENSOR_REG_RO},
	{VEML6046X00_ID_L, "VEML6046X00_ID_L", EMUL_SENSOR_REG_RO, .reset = 0x01},
	{VEML6046X00_ID_H, "VEML6046X00_ID_H", EMUL_SENSOR_REG_RO},
	{INT_FLAG_L, "INT_FLAG_L", EMUL_SENSOR_REG_RO},
	/* Bit 1 G_IF_H, bit 2 G_IF_L; reading the register clears both (AN 80410 Table 12). */
	{INT_FLAG_H, "INT_FLAG_H", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(1) | BIT(2)},
};

/*
 * R_DATA, G_DATA, B_DATA and IR_DATA are unsigned 16-bit counts, low byte at the lower
 * address. RGB_IT, RGB_GAIN and RGB_PDDIV all scale the same digital LSB and are "applied to
 * all four channels simultaneously" (AN 80410, 4.3 Sensitivity Settings); only RGB_IT is
 * modeled as the selectable field since it alone yields exactly the 8 entries this framework
 * supports per channel. The variants below are AN 80410 Table 17's GAIN = x1, PDDIV = 2/2 PD
 * column (register reset values), doubling for each RGB_IT step from 3.125 ms (code 0) to
 * 400 ms (code 7); RGB_GAIN and RGB_PDDIV are stored and readable but do not affect the
 * emulated LSB. AN 80410 6.1 documents the same lx/count resolution for the green channel;
 * the block diagram shows one Integrating ADC per channel sharing RGB_IT/RGB_GAIN/RGB_PDDIV,
 * so the model applies it uniformly to R, G, B and IR.
 */
#define RGBIR(_chan, _reg)                                                                       \
	{.chan = _chan, .reg = _reg, .bits = 16,                                                  \
	 .select = {RGB_CONF_0, GENMASK(6, 4)},                                                   \
	 .variants = {{.lsb = 1.3440}, {.lsb = 0.6720}, {.lsb = 0.3360}, {.lsb = 0.1680},          \
		      {.lsb = 0.0840}, {.lsb = 0.0420}, {.lsb = 0.0210}, {.lsb = 0.0105}}}

static const struct emul_sensor_channel channels[] = {
	RGBIR(SENSOR_CHAN_RED, R_DATA_L),
	RGBIR(SENSOR_CHAN_GREEN, G_DATA_L),
	RGBIR(SENSOR_CHAN_BLUE, B_DATA_L),
	RGBIR(SENSOR_CHAN_IR, IR_DATA_L),
};

/*
 * RGB_ON_0 and RGB_ON_1 must be written together to power the sensor on or off; the model
 * treats RGB_ON_0 (RGB_CONF_0 bit 0) as authoritative for the device .disabled condition
 * below. In auto (selftimed) mode the sensor converts continuously once powered; this mirrors
 * that by converting the retained inputs as soon as RGB_ON_0 clears, matching
 * "When VEML6046X00 wakes up, the data will be refreshed once a new measurement is made"
 * (Auto-Memorization, page 7). Active force mode instead uses RGB_TRIG's convert_on_write
 * above: writing it starts a conversion and it self-clears when done, which is the flag a
 * host polls to detect completion (Table 3).
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool was_off = (old & BIT(0)) != 0U;
	bool now_on = (data->regs[RGB_CONF_0] & BIT(0)) == 0U;
	bool auto_mode = (data->regs[RGB_CONF_0] & BIT(3)) == 0U;

	if (reg == RGB_CONF_0 && was_off && now_on && auto_mode) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = RGB_CONF_0, .mask = BIT(0), .value = BIT(0)},
	.write = write);
