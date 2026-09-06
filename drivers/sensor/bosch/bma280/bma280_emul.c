/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT bosch_bma280

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665

/* BMA280 datasheet BST-BMA280-DS000-10 rev 1.7, section 6.2 register map */
static const struct emul_sensor_reg bma280_regs[] = {
	{0x00, "BGW_CHIPID", EMUL_SENSOR_REG_RO, .reset = 0xFB},
	{0x01, "reserved", EMUL_SENSOR_REG_RO},
	{0x02, "ACCD_X_LSB", EMUL_SENSOR_REG_RO},
	{0x03, "ACCD_X_MSB", EMUL_SENSOR_REG_RO},
	{0x04, "ACCD_Y_LSB", EMUL_SENSOR_REG_RO},
	{0x05, "ACCD_Y_MSB", EMUL_SENSOR_REG_RO},
	{0x06, "ACCD_Z_LSB", EMUL_SENSOR_REG_RO},
	{0x07, "ACCD_Z_MSB", EMUL_SENSOR_REG_RO},
	{0x08, "ACCD_TEMP", EMUL_SENSOR_REG_RO},
	{0x09, "INT_STATUS_0", EMUL_SENSOR_REG_RO},
	{0x0A, "INT_STATUS_1", EMUL_SENSOR_REG_RO},
	{0x0B, "INT_STATUS_2", EMUL_SENSOR_REG_RO},
	{0x0C, "INT_STATUS_3", EMUL_SENSOR_REG_RO},
	{0x0D, "reserved", .reset = 0xFF},
	{0x0E, "FIFO_STATUS", EMUL_SENSOR_REG_RO},
	{0x0F, "PMU_RANGE", .reset = 0x03},
	{0x10, "PMU_BW", .reset = 0x0F},
	{0x11, "PMU_LPW"},
	{0x12, "PMU_LOW_POWER"},
	{0x13, "ACCD_HBW"},
	/* Write only, 0xB6 triggers the reset */
	{0x14, "BGW_SOFTRESET", .self_clear = 0xFF},
	{0x15, "reserved", .reset = 0xFF},
	{0x16, "INT_EN_0"},
	{0x17, "INT_EN_1"},
	{0x18, "INT_EN_2"},
	{0x19, "INT_MAP_0"},
	{0x1A, "INT_MAP_1"},
	{0x1B, "INT_MAP_2"},
	{0x1C, "reserved", .reset = 0xFF},
	{0x1D, "reserved", .reset = 0xFF},
	{0x1E, "INT_SRC"},
	{0x1F, "reserved", .reset = 0xFF},
	{0x20, "INT_OUT_CTRL", .reset = 0x05},
	/* reset_int is write only */
	{0x21, "INT_RST_LATCH", .self_clear = BIT(7)},
	{0x22, "INT_0", .reset = 0x09},
	{0x23, "INT_1", .reset = 0x30},
	{0x24, "INT_2", .reset = 0x81},
	{0x25, "INT_3", .reset = 0x0F},
	{0x26, "INT_4", .reset = 0xC0},
	{0x27, "INT_5"},
	{0x28, "INT_6", .reset = 0x14},
	{0x29, "INT_7", .reset = 0x14},
	{0x2A, "INT_8", .reset = 0x04},
	{0x2B, "INT_9", .reset = 0x0A},
	{0x2C, "INT_A", .reset = 0x18},
	{0x2D, "INT_B", .reset = 0x48},
	{0x2E, "INT_C", .reset = 0x08},
	{0x2F, "INT_D", .reset = 0x11},
	{0x30, "FIFO_CONFIG_0"},
	{0x31, "reserved", .reset = 0xFF},
	{0x32, "PMU_SELF_TEST"},
	/* nvm_remain reads 0xF, nvm_rdy is set, nvm_prog_trig is write only */
	{0x33, "TRIM_NVM_CTRL", .reset = 0xF4, .self_clear = BIT(1)},
	{0x34, "BGW_SPI3_WDT"},
	{0x35, "reserved"},
	/* cal_rdy is set, offset_reset and cal_trigger are write only */
	{0x36, "OFC_CTRL", .reset = 0x10, .self_clear = BIT(7) | GENMASK(6, 5)},
	{0x37, "OFC_SETTING"},
	{0x38, "OFC_OFFSET_X"},
	{0x39, "OFC_OFFSET_Y"},
	{0x3A, "OFC_OFFSET_Z"},
	{0x3B, "TRIM_GP0"},
	{0x3C, "TRIM_GP1"},
	{0x3D, "reserved", .reset = 0xFF},
	{0x3E, "FIFO_CONFIG_1"},
	{0x3F, "FIFO_DATA", EMUL_SENSOR_REG_RO},
};

/*
 * 14-bit two's complement value in bits 15:2 of the LSB/MSB pair, new_data flag in bit 0 of the
 * LSB register. PMU_RANGE.range<3:0> is 0011, 0101, 1000 or 1100 for 2, 4, 8 or 16 g; bits 3:2
 * alone (00, 01, 10, 11) identify the range, at 4096, 2048, 1024 or 512 LSB/g (table 5).
 */
#define ACCEL(_chan, _reg)                                                                         \
	{_chan, .reg = _reg, .is_signed = true, .bits = 14, .pos = 2,                              \
	 .select = {0x0F, GENMASK(3, 2)},                                                          \
	 .variants = {{.lsb = G / 4096}, {.lsb = G / 2048}, {.lsb = G / 1024}, {.lsb = G / 512}},  \
	 .ready = {_reg, BIT(0)}}

static const struct emul_sensor_channel bma280_channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, 0x02),
	ACCEL(SENSOR_CHAN_ACCEL_Y, 0x04),
	ACCEL(SENSOR_CHAN_ACCEL_Z, 0x06),
	/* 0.5 K/LSB, 0x00 is 23 degC (section 4.3.2) */
	{SENSOR_CHAN_DIE_TEMP, .reg = 0x08, .is_signed = true, .bits = 8, .lsb = 0.5,
	 .offset = 23.0, .min = -40.0, .max = 85.0},
};

EMUL_SENSOR_REGMAP_DEFINE(bma280_regs, bma280_channels);
