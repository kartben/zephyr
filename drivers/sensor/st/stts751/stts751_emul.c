/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_stts751

#include <zephyr/drivers/emul_sensor_regmap.h>

/* DocID16483 Rev 7, sections 4 to 4.14: https://www.st.com/resource/en/datasheet/stts751.pdf */
enum {
	TEMPERATURE_VALUE_HIGH_BYTE = 0x00,
	STATUS = 0x01,
	TEMPERATURE_VALUE_LOW_BYTE = 0x02,
	CONFIGURATION = 0x03,
	CONVERSION_RATE = 0x04,
	TEMPERATURE_HIGH_LIMIT_HIGH_BYTE = 0x05,
	TEMPERATURE_HIGH_LIMIT_LOW_BYTE = 0x06,
	TEMPERATURE_LOW_LIMIT_HIGH_BYTE = 0x07,
	TEMPERATURE_LOW_LIMIT_LOW_BYTE = 0x08,
	ONE_SHOT = 0x0f,
	THERM_LIMIT = 0x20,
	THERM_HYSTERESIS = 0x21,
	SMBUS_TIMEOUT_ENABLE = 0x22,
	PRODUCT_ID = 0xfd,
	MANUFACTURER_ID = 0xfe,
	REVISION_NUMBER = 0xff,
};

static const struct emul_sensor_reg registers[] = {
	{TEMPERATURE_VALUE_HIGH_BYTE, "Temperature value high byte", EMUL_SENSOR_REG_RO},
	/* THIGH and TLOW clear on read, THRM and Busy do not */
	{STATUS, "Status", EMUL_SENSOR_REG_RO, .clear_on_read = GENMASK(6, 5)},
	{TEMPERATURE_VALUE_LOW_BYTE, "Temperature value low byte", EMUL_SENSOR_REG_RO},
	/* MASK1, RUN/STOP and Tres1:Tres0; bit 5 must stay 0 and bits 4 and 1:0 are reserved */
	{CONFIGURATION, "Configuration", .write_mask = 0xcc},
	{CONVERSION_RATE, "Conversion rate", .reset = 0x04, .write_mask = 0x0f},
	{TEMPERATURE_HIGH_LIMIT_HIGH_BYTE, "Temperature high limit high byte", .reset = 0x55,
	 .write_mask = 0xff},
	/* The fraction byte holds 1/2 to 1/16 degC in bits 7:4, bits 3:0 read 0 */
	{TEMPERATURE_HIGH_LIMIT_LOW_BYTE, "Temperature high limit low byte", .write_mask = 0xf0},
	{TEMPERATURE_LOW_LIMIT_HIGH_BYTE, "Temperature low limit high byte", .write_mask = 0xff},
	{TEMPERATURE_LOW_LIMIT_LOW_BYTE, "Temperature low limit low byte", .write_mask = 0xf0},
	{ONE_SHOT, "One-shot", .self_clear = 0xff, .write_mask = 0xff, .convert_on_write = 0xff,
	 .requires_standby = true},
	{THERM_LIMIT, "THERM limit", .reset = 0x55, .write_mask = 0xff},
	{THERM_HYSTERESIS, "THERM hysteresis", .reset = 0x0a, .write_mask = 0xff},
	{SMBUS_TIMEOUT_ENABLE, "SMBus timeout enable", .reset = 0x80, .write_mask = 0x80},
	/* 00h on the STTS751-0, 01h on the STTS751-1 */
	{PRODUCT_ID, "Product ID register", EMUL_SENSOR_REG_RO},
	{MANUFACTURER_ID, "Manufacturer ID", EMUL_SENSOR_REG_RO, .reset = 0x53},
	{REVISION_NUMBER, "Revision number", EMUL_SENSOR_REG_RO, .reset = 0x01},
};

/*
 * Two's complement value whose high byte holds the integer part, bit 8 of the pair weighing
 * 1 degC, and whose low byte holds the fraction from 1/2 degC in bit 7 down to 1/16 degC in
 * bit 4 (table 11). Tres1:Tres0 selects 10, 11, 9 or 12 bits, converting that many of the
 * fraction bits at 0.25, 0.125, 0.5 or 0.0625 degC/LSB (table 16).
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMPERATURE_VALUE_HIGH_BYTE, .is_signed = true,
	 .whole_word = true, .min = -40, .max = 125,
	 .select = {CONFIGURATION, GENMASK(3, 2)},
	 .variants = {{.bits = 10, .pos = 6, .lsb = 0.25},
		      {.bits = 11, .pos = 5, .lsb = 0.125},
		      {.bits = 9, .pos = 7, .lsb = 0.5},
		      {.bits = 12, .pos = 4, .lsb = 0.0625}}},
};

/* The status register sits between the two temperature bytes, which are not consecutive. */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr != TEMPERATURE_VALUE_HIGH_BYTE) {
		return true;
	}

	data->regs[TEMPERATURE_VALUE_HIGH_BYTE] = (value >> 8) & 0xffU;
	data->regs[TEMPERATURE_VALUE_LOW_BYTE] = value & 0xffU;

	return false;
}

/* The value written to the one-shot register is a don't care, zero included (section 4.8). */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == ONE_SHOT && (data->regs[CONFIGURATION] & BIT(6)) != 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = BIT(6), .value = BIT(6)},
	.write = write, .sample = sample);
