/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT onnn_nct75

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * NCT75/D Rev. 8, Registers, tables 7-13:
 * https://www.onsemi.com/download/data-sheet/pdf/nct75-d.pdf
 */
enum {
	STORED_TEMPERATURE_VALUE = 0x00,
	CONFIGURATION = 0x01,
	THYST = 0x02,
	TOS = 0x03,
	ONE_SHOT = 0x04,
};

static const struct emul_sensor_reg registers[] = {
	{STORED_TEMPERATURE_VALUE, "Stored Temperature Value", EMUL_SENSOR_REG_RO, .bytes = 2},
	{CONFIGURATION, "Configuration", .bytes = 1, .write_mask = 0xff},
	/* 0x4b00 is +75 C, 0x5000 is +80 C; D3:D0 are not part of a limit (tables 12 and 13) */
	{THYST, "THYST", .bytes = 2, .reset = 0x4b00, .write_mask = 0xfff0},
	{TOS, "TOS", .bytes = 2, .reset = 0x5000, .write_mask = 0xfff0},
	/* The data is irrelevant and is not stored; the write itself starts the conversion */
	{ONE_SHOT, "One-shot", .bytes = 1, .self_clear = 0xff, .convert_on_write = 0xff},
};

static const struct emul_sensor_channel channels[] = {
	/* 12 bit two's complement in D15:D4, 0.0625 C per LSB (tables 6 and 10) */
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = STORED_TEMPERATURE_VALUE, .is_signed = true,
	 .bits = 12, .pos = 4, .lsb = 0.0625, .min = -55, .max = 125},
};

/* A write of 0x00 also starts a conversion, which convert_on_write cannot express. */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	ARG_UNUSED(old);

	if (reg == ONE_SHOT) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	/* Only P2:P0 of the address pointer register select a register (tables 7 and 8) */
	.addr_ignore = 0xf8,
	.disabled = {.reg = CONFIGURATION, .mask = BIT(0), .value = BIT(0)}, .write = write);
