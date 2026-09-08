/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT adi_max30210

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * 19-101474; Rev 0; 12/22, "Register Map" (User Register Map / Register Details, pp. 28-44):
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max30210.pdf
 *
 * The device is byte addressed (each address below holds one 8-bit register; 16-bit values are
 * split across two consecutive addresses, MSB first). A bare START+read resets the address
 * pointer to 0x00 (section "I2C Read Data Format"); a register must be preset with a write
 * before it can be read, so fixed_pointer is not set. The FIFO (0x04-0x0A) stores conversions as
 * a 64-deep queue of 24-bit tagged words read by burst-reading FIFO_DATA; that queue semantics is
 * not modeled and the registers always read the shown reset value. The high/low temperature
 * alarms, their consecutive/nonconsecutive trip counters, and the rate-of-change (TEMP_SLOPE)
 * filter are likewise not modeled; their status bits always read 0.
 */
enum {
	STATUS = 0x00,
	INTERRUPT_ENABLE = 0x02,
	FIFO_WR_PTR = 0x04,
	FIFO_RD_PTR = 0x05,
	OVF_COUNTER = 0x06,
	FIFO_DATA_COUNT = 0x07,
	FIFO_DATA = 0x08,
	FIFO_CONFIG_1 = 0x09,
	FIFO_CONFIG_2 = 0x0a,
	SYSTEM_CONFIGURATION = 0x11,
	PIN_CONFIGURATION = 0x12,
	TEMP_ALARM_HIGH_SETUP = 0x20,
	TEMP_ALARM_LOW_SETUP = 0x21,
	TEMP_ALARM_HIGH_MSB = 0x22,
	TEMP_ALARM_HIGH_LSB = 0x23,
	TEMP_ALARM_LOW_MSB = 0x24,
	TEMP_ALARM_LOW_LSB = 0x25,
	TEMP_INC_FAST_THRESH = 0x26,
	TEMP_DEC_FAST_THRESH = 0x27,
	TEMP_CONFIGURATION_1 = 0x28,
	TEMP_CONFIGURATION_2 = 0x29,
	TEMP_CONVERT = 0x2a,
	TEMP_DATA_MSB = 0x2b,
	TEMP_DATA_LSB = 0x2c,
	TEMP_SLOPE_MSB = 0x2d,
	TEMP_SLOPE_LSB = 0x2e,
	UNIQUE_ID_1 = 0x30,
	UNIQUE_ID_2 = 0x31,
	UNIQUE_ID_3 = 0x32,
	UNIQUE_ID_4 = 0x33,
	UNIQUE_ID_5 = 0x34,
	UNIQUE_ID_6 = 0x35,
	PART_ID = 0xff,
};

static const struct emul_sensor_reg registers[] = {
	/* A_FULL, TEMP_RDY, TEMP_DEC_FAST, TEMP_INC_FAST, TEMP_LO, TEMP_HI, -, PWR_RDY (bit 1
	 * reserved). PWR_RDY is asserted at power-on reset; every bit clears when read.
	 */
	{STATUS, "Status", EMUL_SENSOR_REG_RO, .reset = 0x01, .clear_on_read = 0xfd},
	/* A_FULL_EN, TEMP_RDY_EN, TEMP_DEC_FAST_EN, TEMP_INC_FAST_EN, TEMP_LO_EN, TEMP_HI_EN;
	 * bits 1:0 reserved.
	 */
	{INTERRUPT_ENABLE, "Interrupt Enable", .write_mask = 0xfc},
	{FIFO_WR_PTR, "FIFO Write Pointer", EMUL_SENSOR_REG_RO},
	{FIFO_RD_PTR, "FIFO Read Pointer", EMUL_SENSOR_REG_RO},
	{OVF_COUNTER, "FIFO Counter 1 (OVF_COUNTER)", EMUL_SENSOR_REG_RO},
	{FIFO_DATA_COUNT, "FIFO Counter 2 (FIFO_DATA_COUNT)", EMUL_SENSOR_REG_RO},
	{FIFO_DATA, "FIFO Data", EMUL_SENSOR_REG_RO},
	/* FIFO_A_FULL[5:0]; bits 7:6 reserved. Reset value leaves one free slot (63 samples). */
	{FIFO_CONFIG_1, "FIFO Configuration 1", .reset = 0x1f, .write_mask = 0x3f},
	/* -, FLUSH_FIFO, FIFO_STAT_CLR, A_FULL_TYPE, FIFO_RO, - (bits 7:5 and 0 reserved). */
	{FIFO_CONFIG_2, "FIFO Configuration 2", .write_mask = 0x1e, .self_clear = BIT(4)},
	/* RESET (bit 0, self-clearing); restores every register to its power-on default. */
	{SYSTEM_CONFIGURATION, "System Configuration", .write_mask = BIT(0),
	 .self_clear = BIT(0), .reset_on_write = BIT(0)},
	/* EXT_CVT_EN, EXT_CVT_ICFG, -, -, INT_FCFG[1:0], INT_OCFG[1:0]; bits 5:4 reserved.
	 * EXT_CVT_EN/EXT_CVT_ICFG route conversion start to the CVT/PDB pin, which this emulator
	 * has no bus-visible way to toggle; storing the bits does not simulate that path.
	 */
	{PIN_CONFIGURATION, "Pin Configuration", .reset = 0x04, .write_mask = 0xcf},
	/* TEMP_HI_DET_CNTR[7:5] (RO, not modeled), -, TEMP_HI_TRIP, TEMP_HI_TRIP_CNT[1:0],
	 * TEMP_RST_HI_CNTR (self-clearing, clears the counter).
	 */
	{TEMP_ALARM_HIGH_SETUP, "Temp Alarm High Setup", .write_mask = 0x0f,
	 .self_clear = BIT(0)},
	{TEMP_ALARM_LOW_SETUP, "Temp Alarm Low Setup", .write_mask = 0x0f, .self_clear = BIT(0)},
	/* 0x7FFF = +163.835 degC, the highest code; disables the high alarm at reset. */
	{TEMP_ALARM_HIGH_MSB, "Temp Alarm High MSB", .reset = 0x7f, .write_mask = 0xff},
	{TEMP_ALARM_HIGH_LSB, "Temp Alarm High LSB", .reset = 0xff, .write_mask = 0xff},
	/* 0x8000 = -163.840 degC, the lowest code; disables the low alarm at reset. */
	{TEMP_ALARM_LOW_MSB, "Temp Alarm Low MSB", .reset = 0x80, .write_mask = 0xff},
	{TEMP_ALARM_LOW_LSB, "Temp Alarm Low LSB", .write_mask = 0xff},
	{TEMP_INC_FAST_THRESH, "Temp Increase Fast Threshold", .write_mask = 0xff},
	{TEMP_DEC_FAST_THRESH, "Temp Decrease Fast Threshold", .write_mask = 0xff},
	/* -, -, -, -, CHG_DET_EN, RATE_CHG_FILTER[2:0]. */
	{TEMP_CONFIGURATION_1, "Temp Configuration 1", .write_mask = 0x0f},
	/* ALERT_MODE, -, -, -, TEMP_PERIOD[3:0]. */
	{TEMP_CONFIGURATION_2, "Temp Configuration 2", .write_mask = 0x8f},
	/* -x6, AUTO, CONVERT_T. Writing CONVERT_T as 1 starts a conversion; the bit self-clears
	 * because this model completes conversions instantaneously (real hardware clears it only
	 * once the 8ms integration finishes).
	 */
	{TEMP_CONVERT, "Temp Convert", .write_mask = 0x03, .convert_on_write = BIT(0),
	 .self_clear = BIT(0)},
	{TEMP_DATA_MSB, "Temp Data MSB", EMUL_SENSOR_REG_RO},
	{TEMP_DATA_LSB, "Temp Data LSB", EMUL_SENSOR_REG_RO},
	/* TEMP_SLOPE[8] in bit 0; bits 7:1 reserved. Rate-of-change filter is not modeled. */
	{TEMP_SLOPE_MSB, "Temp Slope MSB", EMUL_SENSOR_REG_RO},
	{TEMP_SLOPE_LSB, "Temp Slope LSB", EMUL_SENSOR_REG_RO},
	/* Factory-programmed 48-bit unique ID; the datasheet gives no fixed value, modeled as 0. */
	{UNIQUE_ID_1, "Unique ID 1", EMUL_SENSOR_REG_RO},
	{UNIQUE_ID_2, "Unique ID 2", EMUL_SENSOR_REG_RO},
	{UNIQUE_ID_3, "Unique ID 3", EMUL_SENSOR_REG_RO},
	{UNIQUE_ID_4, "Unique ID 4", EMUL_SENSOR_REG_RO},
	{UNIQUE_ID_5, "Unique ID 5", EMUL_SENSOR_REG_RO},
	{UNIQUE_ID_6, "Unique ID 6", EMUL_SENSOR_REG_RO},
	{PART_ID, "Part Identifier", EMUL_SENSOR_REG_RO, .reset = 0x45},
};

/*
 * TEMP_DATA[15:0] is a left-justified, sign-extended two's complement code, 0.005 degC/LSB
 * (T = code * 0.005, section "Measuring Temperature"). It is updated, and TEMP_RDY(STATUS[6])
 * asserted, at the end of every conversion regardless of single-shot or autonomous mode; it does
 * not depend on the FIFO being read. Range taken from the -40 to +85 degC operating range
 * (Absolute Maximum Ratings); the code space itself extends to +-163.835/-163.840 degC.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP_DATA_MSB, .is_signed = true, .bits = 16,
	 .lsb = 0.005, .min = -40, .max = 85,
	 .ready = {.reg = STATUS, .mask = BIT(6)}},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true);
