/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT avago_apds9306

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * AV02-4755EN, October 21, 2016, "Register set:" table (page 10) and per-register bit tables
 * (pages 11-14): https://docs.broadcom.com/doc/AV02-4755EN
 */
enum {
	MAIN_CTRL = 0x00,
	ALS_MEAS_RATE = 0x04,
	ALS_GAIN = 0x05,
	PART_ID = 0x06,
	MAIN_STATUS = 0x07,
	CLEAR_DATA_0 = 0x0a,
	CLEAR_DATA_1 = 0x0b,
	CLEAR_DATA_2 = 0x0c,
	ALS_DATA_0 = 0x0d,
	ALS_DATA_1 = 0x0e,
	ALS_DATA_2 = 0x0f,
	INT_CFG = 0x19,
	INT_PERSISTENCE = 0x1a,
	ALS_THRES_UP_0 = 0x21,
	ALS_THRES_UP_1 = 0x22,
	ALS_THRES_UP_2 = 0x23,
	ALS_THRES_LOW_0 = 0x24,
	ALS_THRES_LOW_1 = 0x25,
	ALS_THRES_LOW_2 = 0x26,
	ALS_THRES_VAR = 0x27,
};

static const struct emul_sensor_reg registers[] = {
	/* SW_Reset (bit 4) restarts the whole device; ALS_EN (bit 1) is the standby/active bit */
	{MAIN_CTRL, "MAIN_CTRL", .write_mask = BIT(4) | BIT(1), .reset_on_write = BIT(4)},
	{ALS_MEAS_RATE, "ALS_MEAS_RATE", .reset = 0x22, .write_mask = GENMASK(6, 4) | GENMASK(2, 0)},
	{ALS_GAIN, "ALS_GAIN", .reset = 0x01, .write_mask = GENMASK(2, 0)},
	/* B1HEX on APDS-9306; the APDS-9306-065 variant of the same die reads B3HEX */
	{PART_ID, "PART_ID", EMUL_SENSOR_REG_RO, .reset = 0xb1},
	/* Power-on, ALS interrupt and ALS data status all clear after the register is read */
	{MAIN_STATUS, "MAIN_STATUS", EMUL_SENSOR_REG_RO, .reset = 0x20, .clear_on_read = GENMASK(5, 3)},
	{CLEAR_DATA_0, "CLEAR_DATA_0", EMUL_SENSOR_REG_RO},
	{CLEAR_DATA_1, "CLEAR_DATA_1", EMUL_SENSOR_REG_RO},
	{CLEAR_DATA_2, "CLEAR_DATA_2", EMUL_SENSOR_REG_RO, .write_mask = 0x0f},
	{ALS_DATA_0, "ALS_DATA_0", EMUL_SENSOR_REG_RO},
	{ALS_DATA_1, "ALS_DATA_1", EMUL_SENSOR_REG_RO},
	{ALS_DATA_2, "ALS_DATA_2", EMUL_SENSOR_REG_RO, .write_mask = 0x0f},
	{INT_CFG, "INT_CFG", .reset = 0x10, .write_mask = GENMASK(5, 2)},
	{INT_PERSISTENCE, "INT_PERSISTENCE", .write_mask = GENMASK(7, 4)},
	{ALS_THRES_UP_0, "ALS_THRES_UP_0", .reset = 0xff, .write_mask = 0xff},
	{ALS_THRES_UP_1, "ALS_THRES_UP_1", .reset = 0xff, .write_mask = 0xff},
	{ALS_THRES_UP_2, "ALS_THRES_UP_2", .reset = 0x0f, .write_mask = 0x0f},
	{ALS_THRES_LOW_0, "ALS_THRES_LOW_0", .write_mask = 0xff},
	{ALS_THRES_LOW_1, "ALS_THRES_LOW_1", .write_mask = 0xff},
	{ALS_THRES_LOW_2, "ALS_THRES_LOW_2", .write_mask = 0x0f},
	{ALS_THRES_VAR, "ALS_THRES_VAR", .write_mask = GENMASK(2, 0)},
};

/*
 * ALS_DATA_0/1/2 hold an unsigned integer, 13 to 20 bit, LSB aligned, clipped at 2^resolution - 1
 * (page 12). ALS_MEAS_RATE bits 6:4 select 20/19/18/17/16/13-bit resolution at 400/200/100/50/25/
 * 3.125 ms integration (page 11); the ADC accumulates photocurrent over that time, so counts for
 * a fixed light level scale with it. The description states the output count is a direct,
 * human-eye-weighted readout of illuminance, and Figures 3-9 plot sensor lux equal to meter lux
 * (unity slope) with no other scaling; that calibration point is at the reset default, 18-bit/
 * 100ms, giving 1 lux/count there and 100ms/integration_ms lux/count at the other resolutions.
 * ALS_GAIN (page 11) additionally scales the count 1x/3x/6x/9x/18x but the datasheet gives no
 * lux figure at any gain but the reset default (3x), so gain is stored, not modeled.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_LIGHT, .reg = ALS_DATA_0, .bits = 18, .lsb = 1.0,
	 .min = 0, .max = 262143,
	 .select = {ALS_MEAS_RATE, GENMASK(6, 4)},
	 .variants = {{.bits = 20, .lsb = 0.25, .max = 262143.75},
		      {.bits = 19, .lsb = 0.5, .max = 262143.5},
		      {.bits = 18, .lsb = 1.0, .max = 262143},
		      {.bits = 17, .lsb = 2.0, .max = 262142},
		      {.bits = 16, .lsb = 4.0, .max = 262140},
		      {.bits = 13, .lsb = 32.0, .max = 262112}},
	 .ready = {MAIN_STATUS, BIT(3)}},
};

/*
 * MAIN_CTRL, ALS_MEAS_RATE and ALS_GAIN all "stop the ongoing measurement and start new
 * measurements" (pages 11, 12) while active. ALS_EN going 0 -> 1 needs the same treatment: the
 * device converts continuously once active, but the model only reconverts a retained input on
 * request, so waking from standby must trigger one explicitly.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool als_en = (data->regs[MAIN_CTRL] & BIT(1)) != 0U;
	bool enabling = reg == MAIN_CTRL && (old & BIT(1)) == 0U && als_en;

	if (als_en && (enabling || reg == ALS_MEAS_RATE || reg == ALS_GAIN)) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .byte_addressed = true,
	.disabled = {.reg = MAIN_CTRL, .mask = BIT(1), .value = 0}, .write = write);
