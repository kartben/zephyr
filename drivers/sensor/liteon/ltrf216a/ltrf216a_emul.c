/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT liteon_ltrf216a

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * LTR-F216A, Spec No. DS86-2019-0016 Revision A (V1.4), section 5 Register Set:
 * https://optoelectronics.liteon.com/upload/download/DS86-2019-0016/LTR-F216A_Final_DS_V1.4.pdf
 */
enum {
	MAIN_CTRL = 0x00,
	ALS_MEAS_RES = 0x04,
	ALS_GAIN = 0x05,
	PART_ID = 0x06,
	ALS_STATUS = 0x07,
	CLEAR_DATA_0 = 0x0a,
	CLEAR_DATA_1 = 0x0b,
	CLEAR_DATA_2 = 0x0c,
	ALS_DATA_0 = 0x0d,
	ALS_DATA_1 = 0x0e,
	ALS_DATA_2 = 0x0f,
	INT_CFG = 0x19,
	INT_PST = 0x1a,
	ALS_THRES_UP_0 = 0x21,
	ALS_THRES_UP_1 = 0x22,
	ALS_THRES_UP_2 = 0x23,
	ALS_THRES_LOW_0 = 0x24,
	ALS_THRES_LOW_1 = 0x25,
	ALS_THRES_LOW_2 = 0x26,
};

static const struct emul_sensor_reg registers[] = {
	/* Writing the register stops any ongoing measurement and starts a new one. */
	{MAIN_CTRL, "MAIN_CTRL", .write_mask = BIT(4) | BIT(1), .reset_on_write = BIT(4),
	 .self_clear = BIT(4), .convert_on_write = BIT(1)},
	{ALS_MEAS_RES, "ALS_MEAS_RES", .reset = 0x22, .write_mask = GENMASK(6, 4) | GENMASK(2, 0)},
	{ALS_GAIN, "ALS_GAIN", .reset = 0x01, .write_mask = GENMASK(2, 0)},
	{PART_ID, "PART_ID", EMUL_SENSOR_REG_RO, .reset = 0xb1},
	/* Called MAIN_STATUS in its own section; its three flags clear on read. */
	{ALS_STATUS, "ALS_STATUS", EMUL_SENSOR_REG_RO, .reset = 0x20,
	 .clear_on_read = GENMASK(5, 3)},
	{CLEAR_DATA_0, "CLEAR_DATA_0", EMUL_SENSOR_REG_RO},
	{CLEAR_DATA_1, "CLEAR_DATA_1", EMUL_SENSOR_REG_RO},
	{CLEAR_DATA_2, "CLEAR_DATA_2", EMUL_SENSOR_REG_RO},
	{ALS_DATA_0, "ALS_DATA_0", EMUL_SENSOR_REG_RO},
	{ALS_DATA_1, "ALS_DATA_1", EMUL_SENSOR_REG_RO},
	{ALS_DATA_2, "ALS_DATA_2", EMUL_SENSOR_REG_RO},
	{INT_CFG, "INT_CFG", .reset = 0x10, .write_mask = GENMASK(5, 4) | BIT(2)},
	{INT_PST, "INT_PST", .write_mask = GENMASK(7, 4)},
	{ALS_THRES_UP_0, "ALS_THRES_UP_0", .reset = 0xff, .write_mask = 0xff},
	{ALS_THRES_UP_1, "ALS_THRES_UP_1", .reset = 0xff, .write_mask = 0xff},
	{ALS_THRES_UP_2, "ALS_THRES_UP_2", .reset = 0x0f, .write_mask = GENMASK(3, 0)},
	{ALS_THRES_LOW_0, "ALS_THRES_LOW_0", .write_mask = 0xff},
	{ALS_THRES_LOW_1, "ALS_THRES_LOW_1", .write_mask = 0xff},
	{ALS_THRES_LOW_2, "ALS_THRES_LOW_2", .write_mask = GENMASK(3, 0)},
};

/*
 * Section 7.1: lux = 0.45 * ALS_DATA / (GAIN * INT). GAIN is 1, 3, 6, 9 or 18 from ALS_GAIN
 * and INT is 0.25, 0.5, 1, 2 or 4 for the 16 to 20 bit resolutions of ALS_MEAS_RES, so the
 * lux per count is 0.45 / (GAIN * INT). Only one field can select a variant, so the variants
 * cover the resolution at the reset gain of 3x; the reserved codes 101 to 111 keep the
 * 18 bit default of the channel.
 *
 * The count is unsigned and right aligned over the three data registers, 2^N - 1 full scale:
 * 39321 lux at the 18 bit and 3x reset setting, the 39K lux of section 7.1 note 2.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_LIGHT, .reg = ALS_DATA_0, .whole_word = true, .bits = 18,
	 .lsb = 0.45 / (3 * 1),
	 .select = {.reg = ALS_MEAS_RES, .mask = GENMASK(6, 4)},
	 .variants = {{.bits = 20, .lsb = 0.45 / (3 * 4)},
		      {.bits = 19, .lsb = 0.45 / (3 * 2)},
		      {.bits = 18, .lsb = 0.45 / (3 * 1)},
		      {.bits = 17, .lsb = 0.45 / (3 * 0.5)},
		      {.bits = 16, .lsb = 0.45 / (3 * 0.25)}},
	 .ready = {.reg = ALS_STATUS, .mask = BIT(3)}},
};

/* One byte per register, LSB at the lowest address, 8-bit pointer with byte auto-increment. */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .byte_addressed = true,
	.disabled = {.reg = MAIN_CTRL, .mask = BIT(1), .value = 0});
