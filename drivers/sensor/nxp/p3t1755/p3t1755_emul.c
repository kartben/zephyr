/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT nxp_p3t1755

#include <zephyr/drivers/emul_sensor_regmap.h>

/* P3T1755 Rev. 1.3, sections 7.5-7.6: https://www.nxp.com/docs/en/data-sheet/P3T1755.pdf */
enum { TEMP = 0x00, CONF = 0x01, TLOW = 0x02, THIGH = 0x03 };

static const struct emul_sensor_reg registers[] = {
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO, .bytes = 2},
	{CONF, "CONF", .bytes = 1, .reset = 0x28, .write_mask = 0xff,
		  .convert_on_write = BIT(7), .self_clear = BIT(7)},
	{TLOW, "TLOW", .bytes = 2, .reset = 0x4b00, .write_mask = 0xfff0},
	{THIGH, "THIGH", .bytes = 2, .reset = 0x5000, .write_mask = 0xfff0},
};

static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .is_signed = true, .bits = 12,
	 .lsb = 0.0625,
	 .min = -40, .max = 125, .pos = 4},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONF, .mask = BIT(0), .value = BIT(0)});
