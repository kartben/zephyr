/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT bosch_bmg160

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * BST-BMG160-DS000-09, revision 1.2, section 6.2 register map (figure 8):
 * https://www.mouser.com/datasheet/2/783/BST-BMG160-DS000-09-1221199.pdf
 */
enum {
	CHIP_ID = 0x00, RATE_X_LSB = 0x02, RATE_X_MSB = 0x03, RATE_Y_LSB = 0x04,
	RATE_Y_MSB = 0x05, RATE_Z_LSB = 0x06, RATE_Z_MSB = 0x07, TEMP = 0x08,
	INT_STATUS_0 = 0x09, INT_STATUS_1 = 0x0a, INT_STATUS_2 = 0x0b, INT_STATUS_3 = 0x0c,
	FIFO_STATUS = 0x0e, RANGE = 0x0f, BW = 0x10, LPM1 = 0x11, LPM2 = 0x12,
	RATE_HBW = 0x13, BGW_SOFTRESET = 0x14, INT_EN_0 = 0x15, INT_EN_1 = 0x16,
	INT_MAP_0 = 0x17, INT_MAP_1 = 0x18, INT_MAP_2 = 0x19, INT_RST_LATCH = 0x21,
	HIGH_TH_X = 0x22, HIGH_DUR_X = 0x23, HIGH_TH_Y = 0x24, HIGH_DUR_Y = 0x25,
	HIGH_TH_Z = 0x26, HIGH_DUR_Z = 0x27, SOC = 0x31, A_FOC = 0x32, TRIM_NVM_CTRL = 0x33,
	BGW_SPI3_WDT = 0x34, OFC1 = 0x36, OFC2 = 0x37, OFC3 = 0x38, OFC4 = 0x39,
	TRIM_GP0 = 0x3a, TRIM_GP1 = 0x3b, BIST = 0x3c, FIFO_CONFIG_0 = 0x3d,
	FIFO_CONFIG_1 = 0x3e, FIFO_DATA = 0x3f,
};

static const struct emul_sensor_reg bmg160_regs[] = {
	{CHIP_ID, "CHIP_ID", EMUL_SENSOR_REG_RO, .reset = 0x0f},
	{0x01, "reserved", EMUL_SENSOR_REG_RO},
	{RATE_X_LSB, "RATE_X_LSB", EMUL_SENSOR_REG_RO},
	{RATE_X_MSB, "RATE_X_MSB", EMUL_SENSOR_REG_RO},
	{RATE_Y_LSB, "RATE_Y_LSB", EMUL_SENSOR_REG_RO},
	{RATE_Y_MSB, "RATE_Y_MSB", EMUL_SENSOR_REG_RO},
	{RATE_Z_LSB, "RATE_Z_LSB", EMUL_SENSOR_REG_RO},
	{RATE_Z_MSB, "RATE_Z_MSB", EMUL_SENSOR_REG_RO},
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO},
	{INT_STATUS_0, "INT_STATUS_0", EMUL_SENSOR_REG_RO},
	{INT_STATUS_1, "INT_STATUS_1", EMUL_SENSOR_REG_RO},
	{INT_STATUS_2, "INT_STATUS_2", EMUL_SENSOR_REG_RO},
	{INT_STATUS_3, "INT_STATUS_3", EMUL_SENSOR_REG_RO},
	{0x0d, "reserved", EMUL_SENSOR_REG_RO},
	{FIFO_STATUS, "FIFO_STATUS", EMUL_SENSOR_REG_RO},
	{RANGE, "RANGE"},
	/* bit 7 is read only */
	{BW, "BW", .reset = 0x80, .write_mask = 0x7f},
	{LPM1, "LPM1"},
	{LPM2, "LPM2"},
	/* Figure 8 marks 0x13 write only, its register description marks every bit R/W */
	{RATE_HBW, "RATE_HBW"},
	/* Write only; 0xB6 triggers the reset, other values are ignored */
	{BGW_SOFTRESET, "BGW_SOFTRESET", .self_clear = 0xff, .reset_on_write = 0xb6},
	{INT_EN_0, "INT_EN_0"},
	{INT_EN_1, "INT_EN_1", .reset = 0x0f},
	{INT_MAP_0, "INT_MAP_0"},
	{INT_MAP_1, "INT_MAP_1"},
	/* Figure 8 marks 0x19 write only, its register description marks every bit R/W */
	{INT_MAP_2, "INT_MAP_2"},
	{0x1a, "0x1A"},
	{0x1b, "0x1B", .reset = 0x04},
	{0x1c, "0x1C", .reset = 0xa0},
	{0x1d, "reserved", .reset = 0xc9},
	/* Figure 8 gives 0x08, the register description of 0x1E gives 0x88 */
	{0x1e, "0x1E", .reset = 0x08},
	{0x1f, "reserved", .reset = 0x28},
	{0x20, "reserved"},
	/* reset_int and offset_reset are write only triggers */
	{INT_RST_LATCH, "INT_RST_LATCH", .self_clear = BIT(7) | BIT(6)},
	{HIGH_TH_X, "HIGH_TH_X", .reset = 0x02},
	{HIGH_DUR_X, "HIGH_DUR_X", .reset = 0x19},
	{HIGH_TH_Y, "HIGH_TH_Y", .reset = 0x02},
	{HIGH_DUR_Y, "HIGH_DUR_Y", .reset = 0x19},
	{HIGH_TH_Z, "HIGH_TH_Z", .reset = 0x02},
	{HIGH_DUR_Z, "HIGH_DUR_Z", .reset = 0x19},
	{0x28, "reserved", .reset = 0x24},
	{0x29, "reserved", .reset = 0x19},
	{0x2a, "reserved", .reset = 0xe8},
	{0x2b, "reserved", .reset = 0x22},
	{0x2c, "reserved", .reset = 0x42},
	{0x2d, "reserved", .reset = 0x40},
	{0x2e, "reserved", .reset = 0x81},
	{0x2f, "reserved", .reset = 0xe0},
	{0x30, "reserved", .reset = 0xe8},
	{SOC, "SOC", .reset = 0x60},
	/* fast_offset_en is cleared once the compensation completes (section 4.6.2) */
	{A_FOC, "A_FOC", .reset = 0xc0, .self_clear = BIT(3)},
	/* nvm_remain and nvm_rdy are read only, nvm_prog_trig is a write only trigger */
	{TRIM_NVM_CTRL, "TRIM_NVM_CTRL", .self_clear = BIT(1), .write_mask = 0x0b},
	{BGW_SPI3_WDT, "BGW_SPI3_WDT"},
	{0x35, "reserved"},
	/* 0x36 to 0x3C are NVM images, figure 8 gives no reset value */
	{OFC1, "OFC1"},
	{OFC2, "OFC2"},
	{OFC3, "OFC3"},
	{OFC4, "OFC4"},
	{TRIM_GP0, "TRIM_GP0"},
	{TRIM_GP1, "TRIM_GP1"},
	/* rate_ok, bist_fail and bist_rdy are read only, trig_bist is a write only trigger */
	{BIST, "BIST", .self_clear = BIT(0), .write_mask = BIT(0)},
	{FIFO_CONFIG_0, "FIFO_CONFIG_0"},
	{FIFO_CONFIG_1, "FIFO_CONFIG_1"},
	{FIFO_DATA, "FIFO_DATA", EMUL_SENSOR_REG_RO},
};

#define DEG 0.017453292519943295

/* Table 8: the selected full scale spans +-32767 counts */
#define RATE_LSB(dps) ((dps) * DEG / 32767.0)
#define RATE_FS(dps) ((dps) * DEG)
#define RATE_RANGE(dps) {.lsb = RATE_LSB(dps), .min = -RATE_FS(dps), .max = RATE_FS(dps)}

/*
 * 16-bit two's complement value in the LSB/MSB register pair. RANGE.range<2:0> selects
 * 2000, 1000, 500, 250 or 125 deg/s full scale; the reserved codes 5 to 7 keep the reset
 * range. Sampling stops in suspend and in deep suspend (section 4.2).
 */
#define RATE(_chan, _reg, _ready)                                                                  \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .lsb = RATE_LSB(2000.0),               \
	 .min = -RATE_FS(2000.0), .max = RATE_FS(2000.0), .select = {RANGE, GENMASK(2, 0)},        \
	 .variants = {RATE_RANGE(2000.0), RATE_RANGE(1000.0), RATE_RANGE(500.0),                   \
		      RATE_RANGE(250.0), RATE_RANGE(125.0)},                                       \
	 .ready = {INT_STATUS_1, _ready}, .disabled = {LPM1, BIT(5), BIT(5)}}

static const struct emul_sensor_channel bmg160_channels[] = {
	RATE(SENSOR_CHAN_GYRO_X, RATE_X_LSB, 0),
	RATE(SENSOR_CHAN_GYRO_Y, RATE_Y_LSB, 0),
	/* data_int is raised once the new z-axis sample is stored (section 4.8.4) */
	RATE(SENSOR_CHAN_GYRO_Z, RATE_Z_LSB, BIT(7)),
	/* 0.5 K/LSB, temp of 0x00 is 23 degC (section 4.3.2) */
	{SENSOR_CHAN_DIE_TEMP, .reg = TEMP, .is_signed = true, .bits = 8, .lsb = 0.5,
	 .offset = 23.0, .min = -40.0, .max = 85.0, .disabled = {LPM1, BIT(5), BIT(5)}},
};

/*
 * Section 7.2: one 8-bit pointer byte, one byte per register, rate words read LSB first,
 * and a read restarts at the address of the latest write.
 */
EMUL_SENSOR_REGMAP_DEFINE(bmg160_regs, bmg160_channels, .reg_bytes = 1, .fixed_pointer = true,
	.disabled = {LPM1, BIT(7), BIT(7)});
