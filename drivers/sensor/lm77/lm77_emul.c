/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT lm77

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SNIS103F - JUNE 1999 - REVISED MARCH 2013, "Internal Register Structure" (Figure 8) and
 * Tables 1 to 4: https://www.ti.com/lit/ds/symlink/lm77.pdf
 *
 * The 3-bit Pointer register (Table 1) addresses six registers; P3:P7 must be kept zero, so no
 * other address is valid. The CRIT/HIGH/LOW comparator flags packed into D2:D0 of the
 * Temperature register (Table 2) reflect the window-comparator state against T_LOW/T_HIGH/
 * T_CRIT with hysteresis (section TEMPERATURE COMPARISON); this framework has no primitive for
 * that continuously updated, non-conversion-related state, so those three bits always read back
 * as zero here. The T_CRIT_A and INT open-drain pins they drive are hardware outputs, likewise
 * out of scope for an I2C register model.
 */
enum {
	TEMPERATURE = 0x00,		/* Read only, power-up pointer default */
	CONFIGURATION = 0x01,
	T_HYST = 0x02,
	T_CRIT = 0x03,
	T_LOW = 0x04,
	T_HIGH = 0x05,
};

static const struct emul_sensor_reg registers[] = {
	{TEMPERATURE, "Temperature", EMUL_SENSOR_REG_RO, .bytes = 2},
	/* D4 Fault Queue, D3 INT Polarity, D2 T_CRIT_A Polarity, D1 Interrupt mode, D0 Shutdown
	 * (Table 3). D7:D5 are production-test bits that must stay zero for normal operation.
	 */
	{CONFIGURATION, "Configuration", .bytes = 1, .write_mask = 0x1f},
	/*
	 * D15:D3 hold the 13-bit two's complement trip value at 0.5 degC/LSB, sign-extended from
	 * the same 9-bit+sign ADC format as the Temperature register; D2:D0 are undefined
	 * (Table 4) and are not modeled as writable. Power-up defaults from DEFAULT SETTINGS:
	 * T_HYST = 2 degC (4 << 3 = 0x0020), T_CRIT = 80 degC (160 << 3 = 0x0500),
	 * T_LOW = 10 degC (20 << 3 = 0x00a0), T_HIGH = 64 degC (128 << 3 = 0x0400).
	 */
	{T_HYST, "T_HYST", .bytes = 2, .reset = 0x0020, .write_mask = 0xfff8},
	{T_CRIT, "T_CRIT", .bytes = 2, .reset = 0x0500, .write_mask = 0xfff8},
	{T_LOW, "T_LOW", .bytes = 2, .reset = 0x00a0, .write_mask = 0xfff8},
	{T_HIGH, "T_HIGH", .bytes = 2, .reset = 0x0400, .write_mask = 0xfff8},
};

/*
 * Temperature register (Table 2): D15:D3 is a 13-bit two's complement word at 0.5 degC/LSB
 * (0.5 x 2 = 1 degC step per raw ADC count, 9 bits of magnitude plus sign); D2:D0 are the
 * comparator status bits described above and are not part of the measurement. Full-scale range
 * from the Temperature-to-Digital Converter Accuracy table (Operating Ratings, -55C to +125C).
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMPERATURE, .is_signed = true, .bits = 13,
	 .pos = 3, .lsb = 0.5, .min = -55, .max = 125},
};

/*
 * Shutdown mode stops conversions but the LM77 resumes continuous conversion on its own as soon
 * as Shutdown (D0) is cleared (section SHUTDOWN MODE); no bit is written as one to request it,
 * so convert_on_write cannot express the resume and the transition is detected here instead, as
 * with the STTS751 and TMP451 models.
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == CONFIGURATION && (old & BIT(0)) != 0U &&
	    (data->regs[CONFIGURATION] & BIT(0)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

/* The Pointer register retains its last value across STOP (section INTERNAL REGISTER STRUCTURE). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = BIT(0), .value = BIT(0)},
	.write = write);
