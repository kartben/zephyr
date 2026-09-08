/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT avago_apds9253

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * APDS-9253-001-DS102, March 12, 2019, section "Register Set" (pages 12-21):
 * https://docs.broadcom.com/doc/APDS-9253-001-DS
 */
enum {
	MAIN_CTRL = 0x00,
	LS_MEAS_RATE = 0x04,
	LS_GAIN = 0x05,
	PART_ID = 0x06,
	MAIN_STATUS = 0x07,
	LS_DATA_IR_0 = 0x0a,
	LS_DATA_IR_1 = 0x0b,
	LS_DATA_IR_2 = 0x0c,
	LS_DATA_GREEN_0 = 0x0d,
	LS_DATA_GREEN_1 = 0x0e,
	LS_DATA_GREEN_2 = 0x0f,
	LS_DATA_BLUE_0 = 0x10,
	LS_DATA_BLUE_1 = 0x11,
	LS_DATA_BLUE_2 = 0x12,
	LS_DATA_RED_0 = 0x13,
	LS_DATA_RED_1 = 0x14,
	LS_DATA_RED_2 = 0x15,
	INT_CFG = 0x19,
	INT_PST = 0x1a,
	LS_THRES_UP_0 = 0x21,
	LS_THRES_UP_1 = 0x22,
	LS_THRES_UP_2 = 0x23,
	LS_THRES_LOW_0 = 0x24,
	LS_THRES_LOW_1 = 0x25,
	LS_THRES_LOW_2 = 0x26,
	LS_THRES_VAR = 0x27,
	DK_COUNT_STORAGE_GREEN_ALS = 0x29,
};

static const struct emul_sensor_reg registers[] = {
	/* SAI_LS(5), SW RESET(4, self-clearing), RGB_MODE(2), LS_EN(1); bits 7,6,3,0 reserved */
	{MAIN_CTRL, "MAIN_CTRL", .self_clear = BIT(4), .write_mask = 0x36},
	/* LS_RESOLUTION[6:4], LS_MEASUREMENT_RATE[2:0]; bits 7 and 3 are reserved */
	{LS_MEAS_RATE, "LS_MEAS_RATE", .reset = 0x22, .write_mask = 0x77},
	/* LS_GAIN_RANGE[2:0]; bits 7:3 reserved */
	{LS_GAIN, "LS_GAIN", .reset = 0x01, .write_mask = 0x07},
	{PART_ID, "PART_ID", EMUL_SENSOR_REG_RO, .reset = 0xc2},
	/* POWER_ON_STATUS(5), LS_INTERRUPT_STATUS(4), LS_DATA_STATUS(3); all three clear on read */
	{MAIN_STATUS, "MAIN_STATUS", EMUL_SENSOR_REG_RO, .reset = 0x20, .clear_on_read = 0x38},
	{LS_DATA_IR_0, "LS_DATA_IR_0", EMUL_SENSOR_REG_RO},
	{LS_DATA_IR_1, "LS_DATA_IR_1", EMUL_SENSOR_REG_RO},
	{LS_DATA_IR_2, "LS_DATA_IR_2", EMUL_SENSOR_REG_RO},
	{LS_DATA_GREEN_0, "LS_DATA_GREEN_0", EMUL_SENSOR_REG_RO},
	{LS_DATA_GREEN_1, "LS_DATA_GREEN_1", EMUL_SENSOR_REG_RO},
	{LS_DATA_GREEN_2, "LS_DATA_GREEN_2", EMUL_SENSOR_REG_RO},
	{LS_DATA_BLUE_0, "LS_DATA_BLUE_0", EMUL_SENSOR_REG_RO},
	{LS_DATA_BLUE_1, "LS_DATA_BLUE_1", EMUL_SENSOR_REG_RO},
	{LS_DATA_BLUE_2, "LS_DATA_BLUE_2", EMUL_SENSOR_REG_RO},
	{LS_DATA_RED_0, "LS_DATA_RED_0", EMUL_SENSOR_REG_RO},
	{LS_DATA_RED_1, "LS_DATA_RED_1", EMUL_SENSOR_REG_RO},
	{LS_DATA_RED_2, "LS_DATA_RED_2", EMUL_SENSOR_REG_RO},
	/* LS_INT_SEL[5:4], LS_VAR_MODE(3), LS_INT_EN(2); bits 7,6,1,0 reserved */
	{INT_CFG, "INT_CFG", .reset = 0x10, .write_mask = 0x3c},
	/* LS_PERSIST[6:4]; bits 7 and 3:0 reserved */
	{INT_PST, "INT_PST", .write_mask = 0x70},
	{LS_THRES_UP_0, "LS_THRES_UP_0", .reset = 0xff, .write_mask = 0xff},
	{LS_THRES_UP_1, "LS_THRES_UP_1", .reset = 0xff, .write_mask = 0xff},
	{LS_THRES_UP_2, "LS_THRES_UP_2", .reset = 0x0f, .write_mask = 0x0f},
	{LS_THRES_LOW_0, "LS_THRES_LOW_0", .write_mask = 0xff},
	{LS_THRES_LOW_1, "LS_THRES_LOW_1", .write_mask = 0xff},
	{LS_THRES_LOW_2, "LS_THRES_LOW_2", .write_mask = 0x0f},
	/* LS_THRES_VAR[2:0]; bits 7:3 reserved */
	{LS_THRES_VAR, "LS_THRES_VAR", .write_mask = 0x07},
	/* DARK_VALID(3), DARK_COUNT_GREEN[2:0]; factory-trimmed, datasheet lists no reset value */
	{DK_COUNT_STORAGE_GREEN_ALS, "DK_COUNT_STORAGE_GREEN_ALS", .write_mask = 0x0f},
};

/*
 * ALS/Green channel: unsigned integer, 13 to 20 bit, LSB aligned, across LS_DATA_GREEN_0 (LSB)
 * to LS_DATA_GREEN_2 (MSB, bits 3:0). LS_RESOLUTION in LS_MEAS_RATE[6:4] selects the bit width;
 * the "ALS Gain and Resolution Characteristics" table shows the LSB (lux/count) exactly halving
 * for each added bit at every gain setting. The values below are LS_GAIN = 3x (reset default,
 * LS_GAIN = 01HEX); the 13-bit row is not tabulated there and is derived from that halving
 * relationship (0.722 x 2^3 = 5.776), which reproduces the ~47000 lux full-scale the table shows
 * for every other resolution at this gain. Gain is not modeled: LS_GAIN and LS_RESOLUTION are
 * two independent configuration fields in different registers, and .select only picks one.
 *
 * Red, Blue and IR ADC data (LS_DATA_RED, LS_DATA_BLUE, LS_DATA_IR) have no datasheet LSB-to-lux
 * constant of their own; the RGB Optical Characteristics table only gives each channel's percent
 * irradiance response relative to the IR channel at four test wavelengths. Converting that to lux
 * needs a correlated-color-temperature computation across multiple ADC channels, so these three
 * are left unsupported rather than mapped with a fabricated constant.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_LIGHT, .reg = LS_DATA_GREEN_0, .bits = 18, .lsb = 0.180,
	 .min = 0, .max = 47186,
	 .select = {LS_MEAS_RATE, GENMASK(6, 4)},
	 .variants = {{.bits = 20, .lsb = 0.045, .max = 47186},
		      {.bits = 19, .lsb = 0.090, .max = 47186},
		      {.bits = 18, .lsb = 0.180, .max = 47186},
		      {.bits = 17, .lsb = 0.359, .max = 47054},
		      {.bits = 16, .lsb = 0.722, .max = 47316},
		      {.bits = 13, .lsb = 5.776, .max = 47318}},
	 .ready = {.reg = MAIN_STATUS, .mask = BIT(3)}},
};

/*
 * Writing LS_MEAS_RATE, LS_GAIN or any LS_THRES_UP/LOW register "resets the LS state machine and
 * starts new measurements" per their register descriptions; this only has an effect once the ADCs
 * are powered, i.e. while LS_EN is set. MAIN_CTRL.SW_RESET (bit 4) restores every register to its
 * reset value ("Start Up after Power-On or Software Reset"). Setting LS_EN (bit 1) powers the
 * ADCs on but does not by itself repeat the conversion that produced the retained sample, so the
 * 0 -> 1 transition is converted explicitly here, as with the STTS751 and TMP451 models.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	static const uint8_t restarts[] = {
		LS_MEAS_RATE, LS_GAIN, LS_THRES_UP_0, LS_THRES_UP_1, LS_THRES_UP_2,
		LS_THRES_LOW_0, LS_THRES_LOW_1, LS_THRES_LOW_2,
	};

	if (reg == MAIN_CTRL) {
		if ((data->regs[MAIN_CTRL] & BIT(4)) != 0U) {
			emul_sensor_regmap_reset(target);
			return;
		}
		if ((data->regs[MAIN_CTRL] & BIT(1)) != 0U && (old & BIT(1)) == 0U) {
			emul_sensor_regmap_convert(target);
		}
		return;
	}

	if ((data->regs[MAIN_CTRL] & BIT(1)) == 0U) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(restarts); i++) {
		if (reg == restarts[i]) {
			emul_sensor_regmap_convert(target);
			break;
		}
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = MAIN_CTRL, .mask = BIT(1), .value = 0},
	.write = write);
