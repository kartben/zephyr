/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_ina226

#include <zephyr/drivers/emul_sensor_regmap.h>

/* SBOS547C - JUNE 2011 - REVISED AUGUST 2026, section 7.1 Register Maps:
 * https://www.ti.com/lit/ds/symlink/ina226.pdf
 */
enum {
	CONFIGURATION = 0x00, SHUNT_VOLTAGE = 0x01, BUS_VOLTAGE = 0x02, POWER = 0x03,
	CURRENT = 0x04, CALIBRATION = 0x05, MASK_ENABLE = 0x06, ALERT_LIMIT = 0x07,
	MANUFACTURER_ID = 0xfe, DIE_ID = 0xff,
};

static const struct emul_sensor_reg registers[] = {
	/* Bits 14-12 are unnamed in table 7-2 and keep their power-on value. Writing MODE2 or
	 * MODE1 selects a converting mode, which starts a conversion (section 6.3.1).
	 */
	{CONFIGURATION, "Configuration Register", .reset = 0x4127, .write_mask = 0x8fff,
	 .reset_on_write = BIT(15), .self_clear = BIT(15), .convert_on_write = GENMASK(1, 0)},
	{SHUNT_VOLTAGE, "Shunt Voltage Register", EMUL_SENSOR_REG_RO},
	{BUS_VOLTAGE, "Bus Voltage Register", EMUL_SENSOR_REG_RO},
	{POWER, "Power Register", EMUL_SENSOR_REG_RO},
	{CURRENT, "Current Register", EMUL_SENSOR_REG_RO},
	/* D15 is unnamed, FS14-FS0 hold the calibration value. */
	{CALIBRATION, "Calibration Register", .write_mask = 0x7fff},
	/* Alert enables D15-D10, APOL and LEN are writable; AFF, CVRF and OVF are status bits.
	 * CVRF clears when the register is read.
	 */
	{MASK_ENABLE, "Mask/Enable Register", .write_mask = 0xfc03, .clear_on_read = BIT(3)},
	{ALERT_LIMIT, "Alert Limit Register", .write_mask = 0xffff},
	{MANUFACTURER_ID, "Manufacturer ID Register", EMUL_SENSOR_REG_RO, .reset = 0x5449},
	/* Table 7-1 gives 2260h, or 2261h for a second die COO; table 7-15 shows 0000h. */
	{DIE_ID, "Die ID Register", EMUL_SENSOR_REG_RO, .reset = 0x2260},
};

/* The current and power registers hold Current_LSB = 0.00512 / (Calibration x RSHUNT) and
 * 25 x Current_LSB per bit (equations 1 and 4). RSHUNT is external to the device, so neither
 * register has a scale the datasheet fixes, and both are modeled as storage only.
 */
static const struct emul_sensor_channel channels[] = {
	/* 2.5uV/LSB over the full 16-bit two's complement encoding, +-81.92mV full scale
	 * (section 7.1.2). Reported in volts: sensor.h documents SENSOR_CHAN_VSHUNT as
	 * millivolts, but every in-tree INA driver reports volts. MODE1 selects shunt
	 * conversions.
	 */
	{.chan = SENSOR_CHAN_VSHUNT, .reg = SHUNT_VOLTAGE, .is_signed = true, .bits = 16,
	 .lsb = 0.0000025,
	 .ready = {.reg = MASK_ENABLE, .mask = BIT(3)},
	 .disabled = {.reg = CONFIGURATION, .mask = BIT(0), .value = 0}},
	/* 1.25mV/LSB in BD14-BD0, D15 is always zero (section 7.1.3). The register full scale is
	 * 40.96V but the bus voltage input range is 0V to 36V (section 5.5). MODE2 selects bus
	 * conversions.
	 */
	{.chan = SENSOR_CHAN_VOLTAGE, .reg = BUS_VOLTAGE, .bits = 15, .lsb = 0.00125, .max = 36.0,
	 .ready = {.reg = MASK_ENABLE, .mask = BIT(3)},
	 .disabled = {.reg = CONFIGURATION, .mask = BIT(1), .value = 0}},
};

/* MODE 000 and 100 are both power-down, so MODE2 and MODE1 clear means shutdown (table 7-6). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = GENMASK(1, 0), .value = 0});
