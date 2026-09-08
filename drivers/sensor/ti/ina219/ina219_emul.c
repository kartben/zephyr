/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_ina219

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SBOS448G (August 2008, revised December 2015), section 8.6 Register Maps:
 * https://www.ti.com/lit/ds/symlink/ina219.pdf
 */
enum {
	CONFIGURATION = 0x00,
	SHUNT_VOLTAGE = 0x01,
	BUS_VOLTAGE = 0x02,
	POWER = 0x03,
	CURRENT = 0x04,
	CALIBRATION = 0x05,
};

static const struct emul_sensor_reg registers[] = {
	/* RST (bit 15) self-clears and restores every register to its power-on value. */
	{CONFIGURATION, "Configuration", .bytes = 2, .reset = 0x399f, .write_mask = 0xffff,
	 .reset_on_write = BIT(15)},
	{SHUNT_VOLTAGE, "Shunt voltage", EMUL_SENSOR_REG_RO, .bytes = 2},
	{BUS_VOLTAGE, "Bus voltage", EMUL_SENSOR_REG_RO, .bytes = 2},
	/* CNVR (Bus voltage register bit 1) clears when the Power register is read. */
	{POWER, "Power", EMUL_SENSOR_REG_RO, .bytes = 2,
	 .read_clears = {.reg = BUS_VOLTAGE, .mask = BIT(1)}},
	{CURRENT, "Current", EMUL_SENSOR_REG_RO, .bytes = 2},
	/* FS0 (bit 0) is a void bit, always reads 0 and cannot be written. */
	{CALIBRATION, "Calibration", .bytes = 2, .write_mask = 0xfffe},
};

/*
 * Two's complement 16-bit code; the LSB is always 10 uV (0.01 mV) regardless of the PGA gain
 * (section 8.6.3.1). PG1:PG0 (Configuration bits 12:11) only change the datasheet full-scale
 * rating (Table 4, Figures 20-23), not the bit width or LSB, so only min/max vary per variant.
 * BADC/SADC (Configuration bits 10:7 and 6:3) only change conversion time and averaging count
 * (Table 5); they do not change the stored code, the same reasoning as for the P3T1755 model.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_VSHUNT, .reg = SHUNT_VOLTAGE, .is_signed = true, .bits = 16,
	 .lsb = 0.01, .min = -320, .max = 320,
	 .select = {CONFIGURATION, GENMASK(12, 11)},
	 .variants = {{.min = -40, .max = 40}, {.min = -80, .max = 80},
		      {.min = -160, .max = 160}, {.min = -320, .max = 320}},
	 .ready = {BUS_VOLTAGE, BIT(1)},
	 .disabled = {.reg = CONFIGURATION, .mask = BIT(0), .value = 0}},
	/*
	 * BD12:BD0 sit in bits 15:3 of the register (Figure 24); shifted down, the LSB is
	 * 4 mV. BRNG (bit 13) only sets the datasheet full-scale rating, not the field width.
	 */
	{.chan = SENSOR_CHAN_VOLTAGE, .reg = BUS_VOLTAGE, .bits = 13, .pos = 3, .lsb = 0.004,
	 .min = 0, .max = 32,
	 .select = {CONFIGURATION, BIT(13)},
	 .variants = {{.min = 0, .max = 16}, {.min = 0, .max = 32}},
	 .ready = {BUS_VOLTAGE, BIT(1)},
	 .disabled = {.reg = CONFIGURATION, .mask = BIT(1), .value = 0}},
};

/*
 * Current and Power are not injectable measurements: the device computes them from the Shunt
 * Voltage and Bus Voltage registers and the Calibration register (Equations 4 and 5), and
 * Current_LSB is whatever value the calibration register was programmed for, an arbitrary
 * 16-bit scale factor rather than a handful of enumerable settings. Recompute both whenever an
 * input feeding them changes.
 */
static void update_power_current(const struct emul *target)
{
	struct emul_sensor_regmap_data *data = target->data;
	int16_t shunt = (int16_t)data->regs[SHUNT_VOLTAGE];
	uint16_t cal = (uint16_t)data->regs[CALIBRATION];
	uint16_t bus_code = (uint16_t)data->regs[BUS_VOLTAGE] >> 3;
	int64_t current = ((int64_t)shunt * (int64_t)cal) / 4096;      /* Equation 4 */
	int64_t current_clamped = CLAMP(current, INT16_MIN, INT16_MAX);
	uint64_t magnitude = current_clamped < 0 ? (uint64_t)(-current_clamped)
						  : (uint64_t)current_clamped;
	/* Power is unsigned (Figure 25 has no sign bit): magnitude of current is used. */
	uint64_t power = (magnitude * (uint64_t)bus_code) / 5000;      /* Equation 5 */

	data->regs[CURRENT] = (uint16_t)current_clamped;
	data->regs[POWER] = (uint16_t)MIN(power, UINT16_MAX);

	data->regs[BUS_VOLTAGE] &= ~BIT(0);
	if (current != current_clamped || power > UINT16_MAX) {
		data->regs[BUS_VOLTAGE] |= BIT(0);	/* OVF: math overflow */
	}
}

static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr != SHUNT_VOLTAGE && addr != BUS_VOLTAGE) {
		return true;
	}

	data->regs[addr] = value & 0xffffU;
	update_power_current(target);
	/* Section 8.6.3.2: CNVR is set when a conversion completes and the data can be read. */
	data->regs[BUS_VOLTAGE] |= BIT(1);

	return false;
}

/*
 * A Calibration write immediately rescales the retained Current and Power registers. CNVR
 * (Bus voltage register bit 1) clears on a Configuration write, except into power-down (mode
 * 0) or ADC-off (mode 4) (section 8.6.3.2).
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	uint32_t mode;

	ARG_UNUSED(old);

	if (reg == CALIBRATION) {
		update_power_current(target);
	} else if (reg == CONFIGURATION) {
		mode = data->regs[CONFIGURATION] & GENMASK(2, 0);
		if (mode != 0x0 && mode != 0x4) {
			/*
			 * CNVR clears on the write and is set again once the conversion
			 * completes; conversions are instantaneous here, so the retained
			 * inputs are reconverted and CNVR set in the same step.
			 */
			data->regs[BUS_VOLTAGE] &= ~BIT(1);
			emul_sensor_regmap_convert(target);
			update_power_current(target);
			data->regs[BUS_VOLTAGE] |= BIT(1);
		}
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true, .write = write, .sample = sample);
