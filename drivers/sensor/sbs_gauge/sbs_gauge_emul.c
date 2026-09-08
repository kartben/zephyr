/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT sbs_sbs_gauge

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Smart Battery Data Specification, Revision 1.1, December 11 1998, SBS Implementers Forum,
 * Section 5 "Smart Battery Interface" (command descriptions), Appendix A "The command set in
 * tabular form", Appendix B "Units of Measure", Appendix C "Error Codes":
 * https://sbs-forum.org/specs/sbdat110.pdf
 *
 * All command codes below are SMBus Read Word / Write Word registers (low byte first on the
 * bus, Appendix B), so the whole device uses .reg_bytes = 2 and the default little-endian byte
 * order. Command codes not listed here are reserved or optional per Appendix A and are left out
 * of the table entirely, so the bus correctly returns -EIO for them:
 *
 * - ManufacturerName() 0x20, DeviceName() 0x21, DeviceChemistry() 0x22 and ManufacturerData()
 *   0x23 use the SMBus Read Block protocol: a length byte followed by up to 32 data bytes. This
 *   is not a fixed-width register (rule: no register wider than 4 bytes) and has no numeric
 *   encoding to describe as an emul_sensor_channel, so these commands are not modeled.
 * - OptionalMfgFunction1-5 (0x2f, 0x3c-0x3f) are manufacturer-defined and optional; the
 *   specification defines no behavior for them ("may in no way affect the battery's
 *   conformance", Appendix A note), so they are not modeled.
 * - 0x1d-0x1f, 0x25-0x2e, 0x30-0x3b and everything above 0x3f are reserved (Appendix A note).
 *
 * CAPACITY_MODE (BatteryMode() bit 15) switches RemainingCapacityAlarm(), AtRate(),
 * RemainingCapacity(), FullChargeCapacity() and DesignCapacity() from mAh (charge) to 10mWh
 * (energy); that is a change of physical dimension, not of resolution or full-scale, and
 * converting between the two needs Voltage() (a second channel), so it is not representable by
 * emul_sensor_channel's single SI-units-per-LSB and is not modeled: this emulator always
 * reports capacity-related registers in mAh (CAPACITY_MODE cleared, the power-on default).
 */
enum {
	MANUFACTURER_ACCESS = 0x00,
	REMAINING_CAPACITY_ALARM = 0x01,
	REMAINING_TIME_ALARM = 0x02,
	BATTERY_MODE = 0x03,
	AT_RATE = 0x04,
	AT_RATE_TIME_TO_FULL = 0x05,
	AT_RATE_TIME_TO_EMPTY = 0x06,
	AT_RATE_OK = 0x07,
	TEMPERATURE = 0x08,
	VOLTAGE = 0x09,
	CURRENT = 0x0a,
	AVERAGE_CURRENT = 0x0b,
	MAX_ERROR = 0x0c,
	RELATIVE_STATE_OF_CHARGE = 0x0d,
	ABSOLUTE_STATE_OF_CHARGE = 0x0e,
	REMAINING_CAPACITY = 0x0f,
	FULL_CHARGE_CAPACITY = 0x10,
	RUN_TIME_TO_EMPTY = 0x11,
	AVERAGE_TIME_TO_EMPTY = 0x12,
	AVERAGE_TIME_TO_FULL = 0x13,
	CHARGING_CURRENT = 0x14,
	CHARGING_VOLTAGE = 0x15,
	BATTERY_STATUS = 0x16,
	CYCLE_COUNT = 0x17,
	DESIGN_CAPACITY = 0x18,
	DESIGN_VOLTAGE = 0x19,
	SPECIFICATION_INFO = 0x1a,
	MANUFACTURE_DATE = 0x1b,
	SERIAL_NUMBER = 0x1c,
};

/*
 * BatteryMode() (section 5.1.4): MSB bits 15/14/13 (CAPACITY_MODE/CHARGER_MODE/ALARM_MODE) and
 * 9/8 (PRIMARY_BATTERY/CHARGE_CONTROLLER_ENABLED) are read/write; bits 12:10 and 6:2 are
 * reserved; LSB bit 7 (CONDITION_FLAG) and bits 1:0 (PRIMARY_BATTERY_SUPPORT,
 * INTERNAL_CHARGE_CONTROLLER) are read-only. All bits reset cleared (section 4.4.1).
 */
#define BATTERY_MODE_RW (BIT(15) | BIT(14) | BIT(13) | BIT(9) | BIT(8))

/*
 * BatteryStatus() (section 5.1.21): INITIALIZED (bit 7) is set at manufacture (section 4.4.1)
 * and cleared only if the battery loses its calibration; the alarm and remaining status bits,
 * and the error code nibble, are not derived from the other registers by this model.
 */
static const struct emul_sensor_reg registers[] = {
	{MANUFACTURER_ACCESS, "ManufacturerAccess", .write_mask = 0xffff},
	/* Default is 10% of DesignCapacity() (section 4.4.1); not expressible as a fixed reset. */
	{REMAINING_CAPACITY_ALARM, "RemainingCapacityAlarm", .write_mask = 0xffff},
	{REMAINING_TIME_ALARM, "RemainingTimeAlarm", .reset = 10, .write_mask = 0xffff},
	{BATTERY_MODE, "BatteryMode", .write_mask = BATTERY_MODE_RW},
	{AT_RATE, "AtRate", .write_mask = 0xffff},
	/* Calculated call-set result for AtRate(); 65535 matches AtRate() = 0 (section 5.1.5). */
	{AT_RATE_TIME_TO_FULL, "AtRateTimeToFull", EMUL_SENSOR_REG_RO, .reset = 0xffff},
	{AT_RATE_TIME_TO_EMPTY, "AtRateTimeToEmpty", EMUL_SENSOR_REG_RO, .reset = 0xffff},
	{AT_RATE_OK, "AtRateOK", EMUL_SENSOR_REG_RO, .reset = 1},
	{TEMPERATURE, "Temperature", EMUL_SENSOR_REG_RO},
	{VOLTAGE, "Voltage", EMUL_SENSOR_REG_RO},
	{CURRENT, "Current", EMUL_SENSOR_REG_RO},
	{AVERAGE_CURRENT, "AverageCurrent", EMUL_SENSOR_REG_RO},
	{MAX_ERROR, "MaxError", EMUL_SENSOR_REG_RO},
	{RELATIVE_STATE_OF_CHARGE, "RelativeStateOfCharge", EMUL_SENSOR_REG_RO},
	{ABSOLUTE_STATE_OF_CHARGE, "AbsoluteStateOfCharge", EMUL_SENSOR_REG_RO},
	{REMAINING_CAPACITY, "RemainingCapacity", EMUL_SENSOR_REG_RO},
	{FULL_CHARGE_CAPACITY, "FullChargeCapacity", EMUL_SENSOR_REG_RO},
	{RUN_TIME_TO_EMPTY, "RunTimeToEmpty", EMUL_SENSOR_REG_RO, .reset = 0xffff},
	{AVERAGE_TIME_TO_EMPTY, "AverageTimeToEmpty", EMUL_SENSOR_REG_RO, .reset = 0xffff},
	{AVERAGE_TIME_TO_FULL, "AverageTimeToFull", EMUL_SENSOR_REG_RO, .reset = 0xffff},
	{CHARGING_CURRENT, "ChargingCurrent", EMUL_SENSOR_REG_RO},
	{CHARGING_VOLTAGE, "ChargingVoltage", EMUL_SENSOR_REG_RO},
	{BATTERY_STATUS, "BatteryStatus", EMUL_SENSOR_REG_RO, .reset = BIT(7)},
	{CYCLE_COUNT, "CycleCount", EMUL_SENSOR_REG_RO},
	{DESIGN_CAPACITY, "DesignCapacity", EMUL_SENSOR_REG_RO},
	{DESIGN_VOLTAGE, "DesignVoltage", EMUL_SENSOR_REG_RO},
	/* Revision=0001, Version=0010 (v1.1, no PEC), VScale=IPScale=0 (section 5.1.25). */
	{SPECIFICATION_INFO, "SpecificationInfo", EMUL_SENSOR_REG_RO, .reset = 0x0021},
	{MANUFACTURE_DATE, "ManufactureDate", EMUL_SENSOR_REG_RO},
	{SERIAL_NUMBER, "SerialNumber", EMUL_SENSOR_REG_RO},
};

/*
 * Units per Appendix B and each command's Units/Output line. Voltage(), current, capacity and
 * time registers all encode their SI unit directly (mV, mA, mAh, minutes) so only the /1000
 * (mV to V, mA to A) or /1 (mA already matches SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT's mA)
 * conversions apply. Temperature() is 0.1 K per LSB (section 5.1.9); SENSOR_CHAN_GAUGE_TEMP's
 * doc comment does not state a unit, so this model follows every other Zephyr temperature
 * channel and reports degrees Celsius (K * 0.1 - 273.15).
 *
 * Not modeled as channels, no matching sensor_channel: MaxError() (percent uncertainty, no
 * channel for an error margin), AbsoluteStateOfCharge() (percent of DesignCapacity(); the only
 * matching channel, SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, is already used for
 * RelativeStateOfCharge()), AverageTimeToEmpty() (minutes; the only matching channel,
 * SENSOR_CHAN_GAUGE_TIME_TO_EMPTY, is already used for RunTimeToEmpty()), DesignCapacity()
 * (mAh; no "as-manufactured capacity" channel exists, only *_FULL_CHARGE/REMAINING/NOM_AVAIL/
 * FULL_AVAIL_CAPACITY, none of which mean this), ChargingVoltage() (mV; no "desired charging
 * voltage" channel exists, only DESIGN_VOLTAGE and DESIRED_VOLTAGE, which mean the cell's own
 * rated/nominal voltage, not a value requested from a charger).
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_GAUGE_VOLTAGE, .reg = VOLTAGE, .bits = 16, .lsb = 0.001},
	{.chan = SENSOR_CHAN_CURRENT, .reg = CURRENT, .is_signed = true, .bits = 16,
	 .lsb = 0.001},
	{.chan = SENSOR_CHAN_GAUGE_AVG_CURRENT, .reg = AVERAGE_CURRENT, .is_signed = true,
	 .bits = 16, .lsb = 0.001},
	{.chan = SENSOR_CHAN_GAUGE_TEMP, .reg = TEMPERATURE, .bits = 16, .lsb = 0.1,
	 .offset = -273.15},
	{.chan = SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, .reg = RELATIVE_STATE_OF_CHARGE, .bits = 16,
	 .lsb = 1, .min = 0, .max = 100},
	{.chan = SENSOR_CHAN_GAUGE_REMAINING_CHARGE_CAPACITY, .reg = REMAINING_CAPACITY,
	 .bits = 16, .lsb = 1},
	{.chan = SENSOR_CHAN_GAUGE_FULL_CHARGE_CAPACITY, .reg = FULL_CHARGE_CAPACITY, .bits = 16,
	 .lsb = 1},
	{.chan = SENSOR_CHAN_GAUGE_TIME_TO_EMPTY, .reg = RUN_TIME_TO_EMPTY, .bits = 16, .lsb = 1},
	{.chan = SENSOR_CHAN_GAUGE_TIME_TO_FULL, .reg = AVERAGE_TIME_TO_FULL, .bits = 16,
	 .lsb = 1},
	{.chan = SENSOR_CHAN_GAUGE_CYCLE_COUNT, .reg = CYCLE_COUNT, .bits = 16, .lsb = 1},
	{.chan = SENSOR_CHAN_GAUGE_DESIGN_VOLTAGE, .reg = DESIGN_VOLTAGE, .bits = 16,
	 .lsb = 0.001},
	{.chan = SENSOR_CHAN_GAUGE_DESIRED_CHARGING_CURRENT, .reg = CHARGING_CURRENT, .bits = 16,
	 .lsb = 1},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2);
