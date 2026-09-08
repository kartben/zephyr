/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT rohm_bh1730

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * No.11046EAT13, 2012.02 - Rev.A, Command set / register bit tables (pages 7-10):
 * https://fscdn.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1730fvc-e.pdf
 *
 * The bus does not address registers directly. Every write starts with a Command byte
 * (CMD=1, TRANSACTION[6:5], ADDRESS/special-command[4:0], p.5/7); TRANSACTION=00 selects a
 * register address for the following data byte(s) or a later read, TRANSACTION=11 selects a
 * write-only special command (interrupt output reset, manual integration start/stop, software
 * reset) that has no data phase of its own and takes effect from the Command byte alone. The
 * model strips only the CMD bit (addr_ignore), so TRANSACTION=00 addresses map 1:1 onto the
 * register table below; the special commands have no data byte to trigger a write, so they
 * cannot be expressed through this register-write-driven framework and are not modeled.
 */
enum {
	CONTROL = 0x00,
	TIMING = 0x01,
	INTERRUPT = 0x02,
	THLLOW = 0x03,
	THLHIGH = 0x04,
	THHLOW = 0x05,
	THHHIGH = 0x06,
	GAIN = 0x07,
	ID = 0x12,
	DATA0LOW = 0x14,
	DATA0HIGH = 0x15,
	DATA1LOW = 0x16,
	DATA1HIGH = 0x17,
};

static const struct emul_sensor_reg registers[] = {
	/* ADC_INTR (bit 5) and ADC_VALID (bit 4) are read-only status; RES (7:6) reads back 0 */
	{CONTROL, "CONTROL", .write_mask = 0x0f},
	{TIMING, "TIMING", .reset = 0xda, .write_mask = 0xff},
	/* RES bits 7 and 5 are fixed at 0 */
	{INTERRUPT, "INTERRUPT", .write_mask = 0x5f},
	{THLLOW, "THLLOW", .write_mask = 0xff},
	{THLHIGH, "THLHIGH", .write_mask = 0xff},
	{THHLOW, "THHLOW", .reset = 0xff, .write_mask = 0xff},
	{THHHIGH, "THHHIGH", .reset = 0xff, .write_mask = 0xff},
	/* Bit 2 is a don't care in the value table (X00/X01/X10/X11); RES (7:3) reads back 0 */
	{GAIN, "GAIN", .write_mask = 0x07},
	/* Part number 0111 in bits 7:4; bits 3:0 are an unspecified silicon revision ID */
	{ID, "ID", EMUL_SENSOR_REG_RO, .reset = 0x70},
	{DATA0LOW, "DATA0LOW", EMUL_SENSOR_REG_RO},
	/* ADC_VALID reflects data updated since the last reading; cleared once DATA0 is read */
	{DATA0HIGH, "DATA0HIGH", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = CONTROL, .mask = BIT(4)}},
	{DATA1LOW, "DATA1LOW", EMUL_SENSOR_REG_RO},
	{DATA1HIGH, "DATA1HIGH", EMUL_SENSOR_REG_RO},
};

/*
 * Type0 (visible-peaked, 600 nm, Fig.1) resolution at GAIN=X1/X2/X64/X128 and the TIMING=DAh
 * (default, 218 decimal) reference used throughout the datasheet: rG1=0.83, rG2=0.42,
 * rG64=0.014, rG128=0.007 lx/count (Electrical Characteristics, p.2), consistent with the ALS
 * sensitivity adjustment formula for GAIN=X1: 1/1.2*(256-218)/(256-218) = 0.833 lx/count (p.15).
 * GAIN bit 2 is a don't care in the value table, so the model selects on bits 1:0. TIMING also
 * scales sensitivity (proportional to 256-ITIME) but is a free-running 8-bit field, not a small
 * enumerated set like GAIN, so it is not modeled as a variant; the resolutions above hold at its
 * default value.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_LIGHT, .reg = DATA0LOW, .bits = 16, .max = 54394,
	 .ready = {.reg = CONTROL, .mask = BIT(4)},
	 .select = {.reg = GAIN, .mask = 0x03},
	 .variants = {{.lsb = 0.83, .max = 54394},
		      {.lsb = 0.42, .max = 27525},
		      {.lsb = 0.014, .max = 917.49},
		      {.lsb = 0.007, .max = 458.75}}},
	/*
	 * Type1 (infrared-peaked, 840 nm, Fig.2) has no published lx/count table. Derived from
	 * D1k_1 (typ. 180 count at Ev=1000 lx, GAIN=X1, TIMING=DAh, p.2) with the same Ev/D1k
	 * relation the datasheet uses for rG1 (1000/1200 = 0.833 lx/count), then scaled by gain
	 * the same way as Type0.
	 */
	{.chan = SENSOR_CHAN_IR, .reg = DATA1LOW, .bits = 16, .max = 364083,
	 .ready = {.reg = CONTROL, .mask = BIT(4)},
	 .disabled = {.reg = CONTROL, .mask = BIT(2), .value = BIT(2)},
	 .select = {.reg = GAIN, .mask = 0x03},
	 .variants = {{.lsb = 5.5556, .max = 364083},
		      {.lsb = 2.7778, .max = 182042},
		      {.lsb = 0.086806, .max = 5688.8},
		      {.lsb = 0.043403, .max = 2844.4}}},
};

/*
 * ADC_EN going 0 -> 1 starts a conversion (Measurement Procedure, p.6); the device does not
 * re-convert on its own when it is re-enabled without a fresh ADC_EN write. ONE_TIME performs a
 * single conversion and then "ADC changes to power down automatically" (TIMING register table,
 * p.8), which the model applies by clearing ONE_TIME, ADC_EN and POWER after converting.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	uint32_t val;

	if (reg != CONTROL) {
		return;
	}

	val = data->regs[CONTROL];
	if ((val & BIT(1)) != 0U && (old & BIT(1)) == 0U) {
		emul_sensor_regmap_convert(target);
	}

	if ((val & (BIT(3) | BIT(1) | BIT(0))) == (BIT(3) | BIT(1) | BIT(0))) {
		data->regs[CONTROL] &= ~(uint32_t)(BIT(3) | BIT(1) | BIT(0));
	}
}

/* Command byte: only the CMD bit (bit 7) is stripped, TRANSACTION=00 addresses map directly. */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.addr_ignore = BIT(7), .byte_addressed = true,
	.disabled = {.reg = CONTROL, .mask = BIT(1), .value = 0},
	.write = write);
