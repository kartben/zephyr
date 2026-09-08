/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_adxl313

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665

/*
 * ADXL313 Data Sheet, Rev. C, document D11469-0-2/19(C), "REGISTER MAP" (Table 14), page 17:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/adxl313.pdf
 */
enum {
	DEVID_0 = 0x00,
	DEVID_1 = 0x01,
	PARTID = 0x02,
	REVID = 0x03,
	XID = 0x04,
	SOFT_RESET = 0x18,
	OFSX = 0x1e,
	OFSY = 0x1f,
	OFSZ = 0x20,
	THRESH_ACT = 0x24,
	THRESH_INACT = 0x25,
	TIME_INACT = 0x26,
	ACT_INACT_CTL = 0x27,
	BW_RATE = 0x2c,
	POWER_CTL = 0x2d,
	INT_ENABLE = 0x2e,
	INT_MAP = 0x2f,
	INT_SOURCE = 0x30,
	DATA_FORMAT = 0x31,
	DATA_X0 = 0x32,
	DATA_X1 = 0x33,
	DATA_Y0 = 0x34,
	DATA_Y1 = 0x35,
	DATA_Z0 = 0x36,
	DATA_Z1 = 0x37,
	FIFO_CTL = 0x38,
	FIFO_STATUS = 0x39,
};

/*
 * DATA_READY, WATERMARK and OVERRUN in INT_SOURCE are cleared by reading the data registers
 * (0x32-0x37); ACTIVITY and INACTIVITY are cleared only by reading INT_SOURCE (page 15,
 * "Interrupts"). Activity/inactivity are not modeled, so only the first group is wired up.
 */
#define DATA_READ_CLEARS {.reg = INT_SOURCE, .mask = BIT(7) | BIT(1) | BIT(0)}

static const struct emul_sensor_reg adxl313_regs[] = {
	{DEVID_0, "DEVID_0", EMUL_SENSOR_REG_RO, .reset = 0xad},
	{DEVID_1, "DEVID_1", EMUL_SENSOR_REG_RO, .reset = 0x1d},
	{PARTID, "PARTID", EMUL_SENSOR_REG_RO, .reset = 0xcb},
	{REVID, "REVID", EMUL_SENSOR_REG_RO},
	{XID, "XID", EMUL_SENSOR_REG_RO},
	/* 0x05-0x17: reserved; the datasheet gives no reset value, modeled as read-only zero. */
	{0x05, "Reserved", EMUL_SENSOR_REG_RO}, {0x06, "Reserved", EMUL_SENSOR_REG_RO},
	{0x07, "Reserved", EMUL_SENSOR_REG_RO}, {0x08, "Reserved", EMUL_SENSOR_REG_RO},
	{0x09, "Reserved", EMUL_SENSOR_REG_RO}, {0x0a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x0b, "Reserved", EMUL_SENSOR_REG_RO}, {0x0c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x0d, "Reserved", EMUL_SENSOR_REG_RO}, {0x0e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x0f, "Reserved", EMUL_SENSOR_REG_RO}, {0x10, "Reserved", EMUL_SENSOR_REG_RO},
	{0x11, "Reserved", EMUL_SENSOR_REG_RO}, {0x12, "Reserved", EMUL_SENSOR_REG_RO},
	{0x13, "Reserved", EMUL_SENSOR_REG_RO}, {0x14, "Reserved", EMUL_SENSOR_REG_RO},
	{0x15, "Reserved", EMUL_SENSOR_REG_RO}, {0x16, "Reserved", EMUL_SENSOR_REG_RO},
	{0x17, "Reserved", EMUL_SENSOR_REG_RO},
	/* Writing 0x52 triggers the soft reset (see the write() callback below). */
	{SOFT_RESET, "SOFT_RESET", .write_mask = 0xff},
	/* 0x19-0x1D: reserved. */
	{0x19, "Reserved", EMUL_SENSOR_REG_RO}, {0x1a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1b, "Reserved", EMUL_SENSOR_REG_RO}, {0x1c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1d, "Reserved", EMUL_SENSOR_REG_RO},
	/* Twos complement offset trim, 3.9 mg/LSB; stored but not applied to the output. */
	{OFSX, "OFSX", .write_mask = 0xff},
	{OFSY, "OFSY", .write_mask = 0xff},
	{OFSZ, "OFSZ", .write_mask = 0xff},
	/* 0x21-0x23: reserved. */
	{0x21, "Reserved", EMUL_SENSOR_REG_RO}, {0x22, "Reserved", EMUL_SENSOR_REG_RO},
	{0x23, "Reserved", EMUL_SENSOR_REG_RO},
	{THRESH_ACT, "THRESH_ACT", .write_mask = 0xff},
	{THRESH_INACT, "THRESH_INACT", .write_mask = 0xff},
	{TIME_INACT, "TIME_INACT", .write_mask = 0xff},
	{ACT_INACT_CTL, "ACT_INACT_CTL", .write_mask = 0xff},
	/* 0x28-0x2B: reserved. */
	{0x28, "Reserved", EMUL_SENSOR_REG_RO}, {0x29, "Reserved", EMUL_SENSOR_REG_RO},
	{0x2a, "Reserved", EMUL_SENSOR_REG_RO}, {0x2b, "Reserved", EMUL_SENSOR_REG_RO},
	/* D7:D5 always read 0; LOW_POWER and Rate[3:0] are writable. */
	{BW_RATE, "BW_RATE", .reset = 0x0a, .write_mask = 0x1f},
	/* D7 always reads 0; I2C_DISABLE, Link, AUTO_SLEEP, Measure, Sleep, Wake-up[1:0]. */
	{POWER_CTL, "POWER_CTL", .write_mask = 0x7f},
	{INT_ENABLE, "INT_ENABLE", .write_mask = 0xff},
	{INT_MAP, "INT_MAP", .write_mask = 0xff},
	/* DATA_READY, watermark and overrun are always set by the corresponding event
	 * regardless of INT_ENABLE. Reset is 0x02 because Table 18/FIFO_CTL sets the
	 * watermark condition immediately when the samples bits are 0, which they are
	 * out of reset; this model does not otherwise update the watermark bit.
	 */
	{INT_SOURCE, "INT_SOURCE", EMUL_SENSOR_REG_RO, .reset = 0x02},
	/* D4 always reads 0; SELF_TEST, SPI, INT_INVERT, FULL_RES, Justify, Range[1:0]. */
	{DATA_FORMAT, "DATA_FORMAT", .write_mask = 0xef},
	{DATA_X0, "DATA_X0", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{DATA_X1, "DATA_X1", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{DATA_Y0, "DATA_Y0", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{DATA_Y1, "DATA_Y1", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{DATA_Z0, "DATA_Z0", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{DATA_Z1, "DATA_Z1", EMUL_SENSOR_REG_RO, .read_clears = DATA_READ_CLEARS},
	{FIFO_CTL, "FIFO_CTL", .write_mask = 0xff},
	{FIFO_STATUS, "FIFO_STATUS", EMUL_SENSOR_REG_RO},
};

/*
 * DATA_Xx/DATA_Yx/DATA_Zx hold a 10-bit twos complement value, LSB-justified and sign-extended
 * (Justify = 0, the reset default; left-justified output, and the special LSB placement it gets
 * at 3200 Hz/1600 Hz ODR, are not modeled). DATA_FORMAT.Range<1:0> selects one of four full-scale
 * ranges (Table 16); sensitivity for each is the 10-bit value from Table 1 ("Output Resolution" /
 * "Sensitivity"), which is also what the part defaults to at reset (FULL_RES = 0). FULL_RES = 1
 * (fixed 1024 LSB/g sensitivity with resolution scaling from 10 bits at +-0.5g to 13 bits at
 * +-4g, Table 1) is not modeled.
 */
#define ACCEL(_chan, _reg)                                                                       \
	{_chan, .reg = _reg, .is_signed = true, .bits = 10,                                     \
	 .select = {DATA_FORMAT, GENMASK(1, 0)},                                                \
	 .variants = {{.lsb = G / 1024, .min = -0.5 * G, .max = 0.5 * G},                       \
		      {.lsb = G / 512, .min = -1.0 * G, .max = 1.0 * G},                        \
		      {.lsb = G / 256, .min = -2.0 * G, .max = 2.0 * G},                        \
		      {.lsb = G / 128, .min = -4.0 * G, .max = 4.0 * G}},                       \
	 .ready = {INT_SOURCE, BIT(7)}, .overrun = BIT(0)}

static const struct emul_sensor_channel adxl313_channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, DATA_X0),
	ACCEL(SENSOR_CHAN_ACCEL_Y, DATA_Y0),
	ACCEL(SENSOR_CHAN_ACCEL_Z, DATA_Z0),
};

/* Writing 0x52 restores the power-on register values (page 18, "Register 0x18-SOFT_RESET"). */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == SOFT_RESET && data->regs[SOFT_RESET] == 0x52U) {
		emul_sensor_regmap_reset(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(adxl313_regs, adxl313_channels,
	.disabled = {.reg = POWER_CTL, .mask = BIT(3), .value = 0},
	.write = write);
