/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT vishay_veml6031

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Document Number: 80007, Rev. 1.4, 18-Dec-2025, REGISTER INFORMATION (Tables 1 to 10):
 * https://www.vishay.com/docs/80007/veml6031x00.pdf
 *
 * The device has fourteen defined 8-bit command codes; 0x02, 0x03, 0x08 to 0x0F are not
 * defined. 0x16 (the low byte of INT_FLAG) is listed in Table 2 with a default value and
 * read-only access, so it does respond, even though its bits are all reserved.
 */
enum {
	ALS_CONF_0 = 0x00,
	ALS_CONF_1 = 0x01,
	ALS_THDH_L = 0x04,
	ALS_THDH_H = 0x05,
	ALS_THDL_L = 0x06,
	ALS_THDL_H = 0x07,
	ALS_DATA_L = 0x10,
	ALS_DATA_H = 0x11,
	IR_DATA_L = 0x12,
	IR_DATA_H = 0x13,
	VEML6031X00_ID_L = 0x14,
	VEML6031X00_ID_H = 0x15,
	INT_FLAG_L = 0x16,
	INT_FLAG_H = 0x17,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * Bit 7 reserved; ALS_IT[6:4]; ALS_MODE(3); ALS_TRIG(2, self-clearing active force
	 * trigger); ALS_INT(1); ALS_ON_0(0). Reset 0x01 = shutdown (Table 3).
	 */
	{ALS_CONF_0, "ALS_CONF_0", .reset = 0x01, .write_mask = 0x7f,
	 .self_clear = BIT(2), .convert_on_write = BIT(2)},
	/*
	 * ALS_ON_1(7); ALS_PDDIV(6); bit 5 reserved; ALS_GAIN[4:3]; ALS_PERS[2:1]; ALS_CAL(0).
	 * Reset 0x80 = shutdown (Table 4).
	 */
	{ALS_CONF_1, "ALS_CONF_1", .reset = 0x80, .write_mask = 0xdf},
	{ALS_THDH_L, "ALS_THDH_L", .write_mask = 0xff},
	{ALS_THDH_H, "ALS_THDH_H", .write_mask = 0xff},
	{ALS_THDL_L, "ALS_THDL_L", .write_mask = 0xff},
	{ALS_THDL_H, "ALS_THDL_H", .write_mask = 0xff},
	{ALS_DATA_L, "ALS_DATA_L", EMUL_SENSOR_REG_RO},
	{ALS_DATA_H, "ALS_DATA_H", EMUL_SENSOR_REG_RO},
	{IR_DATA_L, "IR_DATA_L", EMUL_SENSOR_REG_RO},
	{IR_DATA_H, "IR_DATA_H", EMUL_SENSOR_REG_RO},
	/* "Should be kept default" (Table 9); 0x01 for every part. */
	{VEML6031X00_ID_L, "VEML6031X00_ID_L", EMUL_SENSOR_REG_RO, .reset = 0x01},
	/* 0x00 identifies the 0x29 slave address part (VEML6031X00), which DT_DRV_COMPAT covers. */
	{VEML6031X00_ID_H, "VEML6031X00_ID_H", EMUL_SENSOR_REG_RO},
	{INT_FLAG_L, "INT_FLAG_L", EMUL_SENSOR_REG_RO},
	/* Bits 7:4 and 0 reserved; AF_DATA_READY(3); ALS_IF_L(2); ALS_IF_H(1) (Table 10). */
	{INT_FLAG_H, "INT_FLAG_H", EMUL_SENSOR_REG_RO},
};

/*
 * ALS_DATA is a 16-bit unsigned count (Table 7). Its lux/count and full-scale count both
 * depend on ALS_IT, ALS_GAIN and ALS_PDDIV (Tables 11 and 12); a channel has only one select
 * field, so ALS_IT (3 bits, one register, matching the 8-entry variants array exactly) is
 * modeled and ALS_GAIN/ALS_PDDIV are assumed at their reset values, x1 and 4/4 PD, which is
 * the "x1" column of Table 11. At ALS_IT = 3.125 ms footnote 1 says the count no longer
 * saturates at the doubled value, and gives no replacement number, so that variant leaves
 * max at 0 (falls back to the full 16-bit encoding).
 *
 * IR_DATA (0x12/0x13) is a 16-bit unsigned count with the same encoding but no documented
 * lux, or other physical unit, per count: Table 8 only states the raw 0 to 65535 range, and
 * the resolution tables (11, 12) are titled and computed for the ALS channel only. It is left
 * out of the channel list rather than reusing the ALS lux/count figures for it.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_LIGHT, .reg = ALS_DATA_L, .bits = 16, .min = 0,
	 .lsb = 0.6574,
	 .select = {ALS_CONF_0, GENMASK(6, 4)},
	 .variants = {{.lsb = 0.6574},
		      {.lsb = 0.3287, .max = 21542},
		      {.lsb = 0.1644, .max = 10771},
		      {.lsb = 0.0822, .max = 5385},
		      {.lsb = 0.0411, .max = 2693},
		      {.lsb = 0.0205, .max = 1346},
		      {.lsb = 0.0103, .max = 673},
		      {.lsb = 0.0051, .max = 337}},
	 /*
	  * AF_DATA_READY is documented for active force mode; it is the only data-ready-like
	  * flag in the register map and is used here for both modes.
	  */
	 .ready = {INT_FLAG_H, BIT(3)}},
};

/*
 * In auto mode (ALS_MODE = 0) the ADC integrates continuously whenever the device is not
 * shut down; clearing shutdown does not itself write a bit as one, so convert_on_write never
 * fires for it. Detect the ALS_ON_0/ALS_ON_1 bit each register wrote going from 1 to 0 and,
 * if the device is now fully on in auto mode, run a conversion. Active force mode is left to
 * ALS_TRIG, which already converts through convert_on_write.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool was_off, now_on, auto_mode;

	if (reg != ALS_CONF_0 && reg != ALS_CONF_1) {
		return;
	}

	was_off = (reg == ALS_CONF_0) ? (old & BIT(0)) != 0U : (old & BIT(7)) != 0U;
	now_on = (data->regs[ALS_CONF_0] & BIT(0)) == 0U &&
		 (data->regs[ALS_CONF_1] & BIT(7)) == 0U;
	auto_mode = (data->regs[ALS_CONF_0] & BIT(3)) == 0U;

	if (was_off && now_on && auto_mode) {
		emul_sensor_regmap_convert(target);
	}
}

/*
 * ALS_ON_0 (ALS_CONF_0 bit 0) and ALS_ON_1 (ALS_CONF_1 bit 7) must always be written
 * together (Register Addresses note), so they track the same value; the device-level
 * disabled condition is expressed on ALS_ON_0 alone.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = ALS_CONF_0, .mask = BIT(0), .value = BIT(0)},
	.write = write);
