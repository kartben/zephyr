/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT silabs_si7055

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Si7050/1/3/4/5-A20 datasheet Rev. 1.15, table 9 (I2C command table) and section 6 (User
 * Register 1). The device is command based: each command code is modelled as the register the
 * command reads or writes. The optional checksum byte after a measurement is not modelled.
 */
static const struct emul_sensor_reg si7055_regs[] = {
	/* 0x84 0xB8: FWREV, 0x20 is firmware version 2.0 */
	{0x84, "Read Firmware Revision", EMUL_SENSOR_REG_RO, .reset = 0x20},
	{0xE3, "Measure Temperature, Hold Master Mode", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0xE6, "Write User Register 1", .reset = 0x3A},
	{0xE7, "Read User Register 1", .reset = 0x3A},
	{0xF3, "Measure Temperature, No Hold Master Mode", EMUL_SENSOR_REG_RO, .bytes = 2},
	/* 0xFA 0x0F: SNA_3, CRC, SNA_2, CRC, SNA_1, CRC, SNA_0, CRC; all zero serial, CRC-8 0x31 */
	{0xFA, "Read Electronic ID 1st Byte", EMUL_SENSOR_REG_RO, .bytes = 4},
	{0xFB, "SNA_1, CRC, SNA_0, CRC", EMUL_SENSOR_REG_RO, .bytes = 4},
	/* 0xFC 0xC9: SNB_3 = 0x37 (Si7055), SNB_2, CRC of both, then SNB_1, SNB_0, CRC */
	{0xFC, "Read Electronic ID 2nd Byte", EMUL_SENSOR_REG_RO, .bytes = 3, .reset = 0x37001C},
	{0xFD, "SNB_1, SNB_0, CRC", EMUL_SENSOR_REG_RO, .bytes = 3},
	{0xFE, "Reset"},
};

static const struct emul_sensor_channel si7055_channels[] = {
	/*
	 * Temperature (degC) = 175.72 * Temp_Code / 65536 - 46.85, where the two low bits of
	 * Temp_Code always read 0 (section 5.1.1). Only the hold master mode command is driven.
	 */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0xE3, .bits = 14, .pos = 2, .lsb = 175.72 / 16384,
	 .offset = -46.85, .min = -40.0, .max = 125.0},
};

EMUL_SENSOR_REGMAP_DEFINE(si7055_regs, si7055_channels, .big_endian = true);
