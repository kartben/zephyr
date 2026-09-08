/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT isentek_ist8310

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * IST8310 Datasheet, Version 1.2, section 6.4 Registers:
 * https://datasheet.lcsc.com/datasheet/pdf/bb2cac5612aa16ed0d409b965b5d784f.pdf
 */
enum {
	WAI = 0x00,
	STAT1 = 0x02,
	DATAXL = 0x03,
	DATAXH = 0x04,
	DATAYL = 0x05,
	DATAYH = 0x06,
	DATAZL = 0x07,
	DATAZH = 0x08,
	STAT2 = 0x09,
	CNTL1 = 0x0a,
	CNTL2 = 0x0b,
	STR = 0x0c,
	CNTL3 = 0x0d,
	TEMPL = 0x1c,
	TEMPH = 0x1d,
	AVGCNTL = 0x41,
	PDCNTL = 0x42,
};

/* 1 Gauss = 100 uT. */
#define GAUSS_PER_UT (1.0 / 100.0)

static const struct emul_sensor_reg registers[] = {
	/*
	 * The register table (section 6.4.1) prints "10" without a hex suffix, unlike every
	 * hex value elsewhere in the same table ("08h", "0Ah", ...); taken literally that is
	 * decimal 10 (0x0a).
	 */
	/* Section 6.4.2 prints the default as "10" with no base; it is hex, as 0x0A is not a
	 * valid device ID.
	 */
	{WAI, "WAI", EMUL_SENSOR_REG_RO, .reset = 0x10},
	/* DOR (bit 1) clears on any output data register read; DRDY (bit 0) also on STAT2. */
	{STAT1, "STAT1", EMUL_SENSOR_REG_RO},
	{DATAXL, "DATAXL", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	{DATAXH, "DATAXH", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	{DATAYL, "DATAYL", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	{DATAYH, "DATAYH", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	{DATAZL, "DATAZL", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	{DATAZH, "DATAZH", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, 0x03}},
	/* STAT2 read only clears DRDY (section 3.1.2); DOR is only released by a data read. */
	{STAT2, "STAT2", EMUL_SENSOR_REG_RO, .read_clears = {STAT1, BIT(0)}},
	/* Mode[3:0]: 0000 stand-by, 0001 single measurement, self-clears back to 0000. */
	{CNTL1, "CNTL1", .write_mask = 0x0f, .convert_on_write = BIT(0), .self_clear = BIT(0)},
	/* DREN=1, DRP=1 (active high) at reset; SRST self-clears after the POR routine. */
	{CNTL2, "CNTL2", .reset = 0x0c, .write_mask = 0x0d, .self_clear = BIT(0)},
	{STR, "STR", .write_mask = BIT(6)},
	/*
	 * Not in the section 6.4.1 register table. The part answers at 0x0D and drivers use it to
	 * select 16-bit output resolution per axis, so it is modeled as plain storage.
	 */
	{CNTL3, "CNTL3", .write_mask = 0xff},
	{TEMPL, "TEMPL", EMUL_SENSOR_REG_RO},
	{TEMPH, "TEMPH", EMUL_SENSOR_REG_RO},
	{AVGCNTL, "AVGCNTL", .write_mask = 0x3f},
	{PDCNTL, "PDCNTL", .write_mask = 0xc0},
};

/*
 * Section 6.4.4: 2's complement X/Y/Z data, low byte at the lower address. Section 4.4:
 * resolution (RESO) 0.3 uT/LSB typical, dynamic range +/-1600 uT on X and Y, +/-2500 uT on Z
 * (MDR_XY, MDR_Z). DRDY (STAT1 bit 0) and DOR/overrun (STAT1 bit 1) are shared by all axes,
 * set together since the three axes are sampled and stored as one measurement.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_MAGN_X, .reg = DATAXL, .is_signed = true, .bits = 16,
	 .lsb = 0.3 * GAUSS_PER_UT, .min = -1600 * GAUSS_PER_UT, .max = 1600 * GAUSS_PER_UT,
	 .ready = {STAT1, BIT(0)}, .overrun = BIT(1)},
	{.chan = SENSOR_CHAN_MAGN_Y, .reg = DATAYL, .is_signed = true, .bits = 16,
	 .lsb = 0.3 * GAUSS_PER_UT, .min = -1600 * GAUSS_PER_UT, .max = 1600 * GAUSS_PER_UT,
	 .ready = {STAT1, BIT(0)}, .overrun = BIT(1)},
	{.chan = SENSOR_CHAN_MAGN_Z, .reg = DATAZL, .is_signed = true, .bits = 16,
	 .lsb = 0.3 * GAUSS_PER_UT, .min = -2500 * GAUSS_PER_UT, .max = 2500 * GAUSS_PER_UT,
	 .ready = {STAT1, BIT(0)}, .overrun = BIT(1)},
};

/* CNTL2 SRST performs the full POR routine (section 6.4.7), not just this register. */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == CNTL2 && (data->regs[CNTL2] & BIT(0)) != 0U) {
		emul_sensor_regmap_reset(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = CNTL1, .mask = 0x0f, .value = 0x00},
	.write = write);
