/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT microchip_tcn75a

#include "regmap.h"

/* DS21935D, sections 5.3-5.4: https://ww1.microchip.com/downloads/en/DeviceDoc/21935D.pdf */
enum { TA = 0x00, CONFIG = 0x01, THYST = 0x02, TSET = 0x03 };

static const struct emul_regmap_register registers[] = {
	[TA] = {.bytes = 2},
	[CONFIG] = {.bytes = 1, .write_mask = 0xff, .convert_on_write = BIT(7),
		    .self_clear = BIT(7), .requires_standby = true},
	/* Register diagrams 5-4/5-5 specify 75/80 C; Table 5-4 hex values disagree. */
	[THYST] = {.bytes = 2, .reset = 0x4b00, .write_mask = 0xff80},
	[TSET] = {.bytes = 2, .reset = 0x5000, .write_mask = 0xff80},
};

static const struct emul_regmap_range resolutions[] = {
	{.lsb = 0.5, .shift = 7}, {.lsb = 0.25, .shift = 6},
	{.lsb = 0.125, .shift = 5}, {.lsb = 0.0625, .shift = 4},
};

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_AMBIENT_TEMP, .reg = TA, .min = -40, .max = 125,
	 .range_select = {.reg = CONFIG, .mask = 0x60},
	 .ranges = resolutions, .range_count = ARRAY_SIZE(resolutions)},
};

EMUL_REGMAP_MODEL(registers, channels,
	.disabled = {.reg = CONFIG, .mask = BIT(0), .value = BIT(0)});
