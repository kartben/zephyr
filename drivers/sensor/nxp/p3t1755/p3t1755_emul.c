/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_p3t1755

#include <zephyr/drivers/emul_sensor_regmap.h>

/* P3T1755 datasheet Rev. 1.3, table 13 */
static const struct emul_sensor_reg p3t1755_regs[] = {
	{0x00, "Temp", EMUL_SENSOR_REG_RO},
	{0x01, "Conf", .bytes = 1, .reset = 0x28},
	{0x02, "TLOW", .reset = 0x4B00},
	{0x03, "THIGH", .reset = 0x5000},
};

static const struct emul_sensor_channel p3t1755_channels[] = {
	/* 12-bit two's complement in bits 15:4, 0.0625 degC/LSB */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .bits = 12, .pos = 4,
	 .lsb = 0.0625, .min = -40.0, .max = 125.0},
};

EMUL_SENSOR_REGMAP_DEFINE(p3t1755_regs, p3t1755_channels, .reg_bytes = 2, .big_endian = true);
