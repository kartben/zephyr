/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT silabs_si7060

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Si7060 Data Sheet, Preliminary Rev. 0.1, Section 3, Register Interface:
 * https://www.silabs.com/documents/public/data-sheets/si7060-datasheet.pdf
 *
 * "The Si7060 has 9 registers. 0xC0 through 0xC9 not including 0xC3." 0xC3 is a documented
 * "Do not use" address; 0xC8 is one of the 9 but carries no named field. 0xE1-0xE3 are a
 * separate OTP read-back interface described in the same section.
 */
enum {
	CHIPID_REVID = 0xc0, /* chipid[7:4] (RO), revid[3:0] (RO) */
	DSPSIGM = 0xc1,       /* fresh[7], Dspsigm[6:0]: MSB of the conversion result (RO) */
	DSPSIGL = 0xc2,       /* Dspsigl[7:0]: LSB of the conversion result (RO) */
	RESERVED_C3 = 0xc3,   /* "Do not use" */
	CTRL4 = 0xc4,         /* meas(RO), usestore, oneburst, stop, sleep */
	CTRL5 = 0xc5,         /* arautoinc */
	CTRL6 = 0xc6,         /* sw_low4temp, sw_op */
	CTRL7 = 0xc7,         /* fixed 0x3, sw_hyst */
	RESERVED_C8 = 0xc8,   /* counted among the 9 registers, no named field */
	CTRL9 = 0xc9,         /* slTimeena */
	OTP_ADDR = 0xe1,
	OTP_DATA = 0xe2,
	OTP_CTRL = 0xe3,      /* otp_read (self-clearing), otp_busy (RO) */
};

static const struct emul_sensor_reg registers[] = {
	{CHIPID_REVID, "chipid/revid", EMUL_SENSOR_REG_RO, .reset = 0x14},
	/* Reading Dspsigm loads Dspsigl with the paired LSB; the fresh bit clears on that read. */
	{DSPSIGM, "Dspsigm", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(7)},
	{DSPSIGL, "Dspsigl", EMUL_SENSOR_REG_RO},
	{RESERVED_C3, "Do not use", EMUL_SENSOR_REG_RO},
	{CTRL4, "usestore/oneburst/stop/sleep", .write_mask = BIT(3) | BIT(2) | BIT(1) | BIT(0),
	 .convert_on_write = BIT(2)},
	/* arautoinc is not retained in sleep mode, so it also resets to 0 on wake. */
	{CTRL5, "arautoinc", .write_mask = BIT(0)},
	/*
	 * sw_low4temp defaults to 1 (section 3). sw_op is documented as a 9 bit number
	 * defaulting to 312, but the register map allocates it only 7 bits (sw_op[6:0]) here;
	 * 312 does not fit that width, so no reset value for sw_op can be derived from the
	 * datasheet. sw_low4temp/sw_op only steer the analog alert-comparator output pin, which
	 * has no sensor_channel equivalent and is not otherwise modeled.
	 */
	{CTRL6, "sw_low4temp/sw_op", .reset = BIT(7)},
	/* Bits 7:6 read as the fixed value 0x3 (not documented as writable). sw_hyst defaults to
	 * 28 (hysteresis = 0.025 * (8 + 4) * 2^3 = 2.4 degC, matching the stated default).
	 */
	{CTRL7, "sw_hyst", .reset = (0x3 << 6) | 28, .write_mask = GENMASK(5, 0)},
	{RESERVED_C8, "Reserved"},
	{CTRL9, "slTimeena", .reset = BIT(0), .write_mask = BIT(0)},
	{OTP_ADDR, "otp_addr", .write_mask = 0xff},
	{OTP_DATA, "otp_data", EMUL_SENSOR_REG_RO},
	{OTP_CTRL, "otp_read/otp_busy", .write_mask = BIT(1), .self_clear = BIT(1)},
};

/*
 * "The complete 15b unsigned result is 256*Dspsigm[6:0]+Dspsigl[7:0]." T(C) = 55 +
 * (raw-16384)/160, so 1 LSB = 1/160 degC and raw=0 is 55 - 16384/160 = -47.4 degC. Recommended
 * operating range -40 to 125 degC (Table 4.1) is used as the channel range; the raw encoding
 * clamps at -47.4/157.39 degC per section 2.2 but never reaches those limits within spec.
 * Bit 15 of the 16-bit Dspsigm:Dspsigl word is the fresh/ready flag, not sample data.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = DSPSIGM, .bits = 15, .pos = 0,
	 .lsb = 1.0 / 160, .offset = 55.0 - 16384.0 / 160,
	 .min = -40, .max = 125,
	 .ready = {.reg = DSPSIGM, .mask = BIT(7)}},
};

/*
 * Oneburst (convert_on_write) triggers a conversion; the device then sets stop to 1 (section
 * 3, "The stop bit will be set to 1 once the measurement is complete"). Clearing sleep or stop
 * "restarts the measurement loop" (section 3): the continuously-converting state does not
 * reconvert on its own when either bit is cleared, so the retained inputs are converted here.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	uint32_t val;

	if (reg != CTRL4) {
		return;
	}

	val = data->regs[CTRL4];

	if ((val & BIT(2)) != 0U && (old & BIT(2)) == 0U) {
		data->regs[CTRL4] |= BIT(1);
	}

	if (((old & BIT(0)) != 0U && (val & BIT(0)) == 0U) ||
	    ((old & BIT(1)) != 0U && (val & BIT(1)) == 0U)) {
		emul_sensor_regmap_convert(target);
	}
}

/*
 * arautoinc (section 2) is the byte-increment control for multi-register transfers; sleep is
 * the "master bit" that stops conversions (section 1), overriding stop.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.increment = {CTRL5, BIT(0)},
	.disabled = {.reg = CTRL4, .mask = BIT(0), .value = BIT(0)},
	.write = write);
