/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT maxim_max31875

#include <zephyr/drivers/emul_sensor_regmap.h>

/* MAX31875 datasheet 19-100110 Rev 4, tables 2 to 6; 16-bit registers, MSB first */
static const struct emul_sensor_reg max31875_regs[] = {
	{0x00, "Temperature", EMUL_SENSOR_REG_RO},
	/* One-Shot (D0) returns to 0 when the conversion completes */
	{0x01, "Configuration", .reset = 0x0040, .self_clear = BIT(0)},
	{0x02, "THYST", .reset = 0x4B00},
	{0x03, "TOS", .reset = 0x5000},
};

static const struct emul_sensor_channel max31875_channels[] = {
	/*
	 * Configuration D7 (Data Format) and D6:D5 (Resolution) together pick the field: the
	 * resolution sets the LSB (1, 0.5, 0.25 or 0.0625 degC) and the extended format adds a
	 * 128 degC bit below the sign so that the data word grows by one bit at the same LSB.
	 * The -50 to +150 degC operating range is only reachable in the extended format; the
	 * normal (POR) format stops at 128 degC - 1 LSB, 127 degC at 8-bit resolution.
	 */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .min = -50.0, .max = 127.0,
	 .select = {0x01, GENMASK(7, 5)},
	 .variants = {{.bits = 8, .pos = 8, .lsb = 1.0},
		      {.bits = 9, .pos = 7, .lsb = 0.5},
		      {.bits = 10, .pos = 6, .lsb = 0.25},
		      {.bits = 12, .pos = 4, .lsb = 0.0625},
		      {.bits = 9, .pos = 7, .lsb = 1.0},
		      {.bits = 10, .pos = 6, .lsb = 0.5},
		      {.bits = 11, .pos = 5, .lsb = 0.25},
		      {.bits = 13, .pos = 3, .lsb = 0.0625}}},
};

EMUL_SENSOR_REGMAP_DEFINE(max31875_regs, max31875_channels, .reg_bytes = 2, .big_endian = true);
