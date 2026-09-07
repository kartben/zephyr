/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT microchip_tcn75a

#include <zephyr/drivers/emul_sensor_regmap.h>

/* DS21935D, sections 5.3-5.4: https://ww1.microchip.com/downloads/en/DeviceDoc/21935D.pdf */
enum { TA = 0x00, CONFIG = 0x01, THYST = 0x02, TSET = 0x03 };

static const struct emul_sensor_reg registers[] = {
	{TA, "TA", EMUL_SENSOR_REG_RO, .bytes = 2},
	{CONFIG, "CONFIG", .bytes = 1, .write_mask = 0xff, .convert_on_write = BIT(7),
		    .self_clear = BIT(7), .requires_standby = true},
	/* Register diagrams 5-4/5-5 specify 75/80 C; Table 5-4 hex values disagree. */
	{THYST, "THYST", .bytes = 2, .reset = 0x4b00, .write_mask = 0xff80},
	{TSET, "TSET", .bytes = 2, .reset = 0x5000, .write_mask = 0xff80},
};

static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TA, .is_signed = true, .whole_word = true,
	 .min = -40, .max = 125,
	 .select = {.reg = CONFIG, .mask = 0x60},
	 .variants = {{.bits = 9, .pos = 7, .lsb = 0.5},
		      {.bits = 10, .pos = 6, .lsb = 0.25},
		      {.bits = 11, .pos = 5, .lsb = 0.125},
		      {.bits = 12, .pos = 4, .lsb = 0.0625}}},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONFIG, .mask = BIT(0), .value = BIT(0)});
