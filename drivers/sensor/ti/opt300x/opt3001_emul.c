/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_opt3001

#include <zephyr/drivers/emul_sensor_regmap.h>

/* SBOS681C Rev. C, section 7.6: https://www.ti.com/lit/ds/symlink/opt3001.pdf */
enum {
	RESULT = 0x00, CONFIGURATION = 0x01, LOW_LIMIT = 0x02, HIGH_LIMIT = 0x03,
	MANUFACTURER_ID = 0x7e, DEVICE_ID = 0x7f,
};

static const struct emul_sensor_reg registers[] = {
	{RESULT, "Result", EMUL_SENSOR_REG_RO},
	/* OVF, CRF, FH and FL are read only; M[1:0] = 01b converts once and returns to 00b. */
	{CONFIGURATION, "Configuration", .reset = 0xc810, .write_mask = 0xfe1f,
	 .clear_on_read = BIT(7), .convert_on_write = BIT(9), .self_clear = BIT(9)},
	/* Section 7.6.1.1.3 heading prints a five-digit C0000h; Table 11 gives 0000h. */
	{LOW_LIMIT, "Low Limit", .write_mask = 0xffff},
	{HIGH_LIMIT, "High Limit", .reset = 0xbfff, .write_mask = 0xffff},
	{MANUFACTURER_ID, "Manufacturer ID", EMUL_SENSOR_REG_RO, .reset = 0x5449},
	{DEVICE_ID, "Device ID", EMUL_SENSOR_REG_RO, .reset = 0x3001},
};

/*
 * lux = 0.01 x 2^E[3:0] x R[11:0] (equation 3), so the exponent held in the result register
 * itself scales the 12-bit mantissa. LSB size and full scale per exponent are Table 8; codes
 * 8h to Bh (2.56 to 20.48 lux per LSB) do not fit the eight variant slots.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_LIGHT, .reg = RESULT, .bits = 12,
	 .select = {.reg = RESULT, .mask = GENMASK(15, 12)},
	 .variants = {{.lsb = 0.01, .max = 40.95},
		      {.lsb = 0.02, .max = 81.90},
		      {.lsb = 0.04, .max = 163.80},
		      {.lsb = 0.08, .max = 327.60},
		      {.lsb = 0.16, .max = 655.20},
		      {.lsb = 0.32, .max = 1310.40},
		      {.lsb = 0.64, .max = 2620.80},
		      {.lsb = 1.28, .max = 5241.60}},
	 .ready = {.reg = CONFIGURATION, .mask = BIT(7)}},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = GENMASK(10, 9), .value = 0});
