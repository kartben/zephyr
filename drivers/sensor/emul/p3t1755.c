/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT nxp_p3t1755

#include "regmap.h"

/* P3T1755 Rev. 1.3, sections 7.5-7.6: https://www.nxp.com/docs/en/data-sheet/P3T1755.pdf */
enum { TEMP = 0x00, CONF = 0x01, TLOW = 0x02, THIGH = 0x03 };

static const struct emul_regmap_register registers[] = {
	[TEMP] = {.bytes = 2},
	[CONF] = {.bytes = 1, .reset = 0x28, .write_mask = 0xff,
		  .convert_on_write = BIT(7), .self_clear = BIT(7)},
	[TLOW] = {.bytes = 2, .reset = 0x4b00, .write_mask = 0xfff0},
	[THIGH] = {.bytes = 2, .reset = 0x5000, .write_mask = 0xfff0},
};

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .lsb = 0.0625,
	 .min = -40, .max = 125, .shift = 4},
};

EMUL_REGMAP_MODEL(registers, channels,
	.disabled = {.reg = CONF, .mask = BIT(0), .value = BIT(0)});
