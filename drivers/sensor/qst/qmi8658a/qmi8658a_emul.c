/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT qst_qmi8658a

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G   9.80665
/* Radians per degree, for the dps sensitivities of table 9. */
#define DPS (3.14159265358979323846 / 180.0)

/*
 * QST-PD-B002-22 QMI8658A Datasheet Rev: D, section 4.1 register map (table 20), with the bit
 * descriptions of sections 5.2 - 5.9:
 * https://www.qstcorp.com/upload/pdf/202512/13-52-25%20QMI8658A%20Datasheet%20Rev%20D.pdf
 */
enum {
	WHO_AM_I = 0x00, REVISION_ID = 0x01,
	CTRL1 = 0x02, CTRL2 = 0x03, CTRL3 = 0x04, RESERVED_05 = 0x05, CTRL5 = 0x06,
	RESERVED_07 = 0x07, CTRL7 = 0x08, CTRL8 = 0x09, CTRL9 = 0x0a,
	CAL1_L = 0x0b, CAL1_H = 0x0c, CAL2_L = 0x0d, CAL2_H = 0x0e,
	CAL3_L = 0x0f, CAL3_H = 0x10, CAL4_L = 0x11, CAL4_H = 0x12,
	FIFO_WTM_TH = 0x13, FIFO_CTRL = 0x14, FIFO_SMPL_CNT = 0x15, FIFO_STATUS = 0x16,
	FIFO_DATA = 0x17,
	STATUSINT = 0x2d, STATUS0 = 0x2e, STATUS1 = 0x2f,
	TIMESTAMP_LOW = 0x30, TIMESTAMP_MID = 0x31, TIMESTAMP_HIGH = 0x32,
	TEMP_L = 0x33, TEMP_H = 0x34,
	AX_L = 0x35, AX_H = 0x36, AY_L = 0x37, AY_H = 0x38, AZ_L = 0x39, AZ_H = 0x3a,
	GX_L = 0x3b, GX_H = 0x3c, GY_L = 0x3d, GY_H = 0x3e, GZ_L = 0x3f, GZ_H = 0x40,
	COD_STATUS = 0x46,
	DQW_L = 0x49, DQW_H = 0x4a, DQX_L = 0x4b, DQX_H = 0x4c,
	DQY_L = 0x4d, DQY_H = 0x4e, DQZ_L = 0x4f, DQZ_H = 0x50,
	DVX_L = 0x51, DVX_H = 0x52, DVY_L = 0x53, DVY_H = 0x54, DVZ_L = 0x55, DVZ_H = 0x56,
	TAP_STATUS = 0x59, RESET = 0x60,
};

static const struct emul_sensor_reg registers[] = {
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x05},
	{REVISION_ID, "REVISION_ID", EMUL_SENSOR_REG_RO, .reset = 0x7c},
	/* Bit 1 is reserved; BE and ADDR_AI are bits 5 and 6 of the 0x20 reset value. */
	{CTRL1, "CTRL1", .reset = 0x20, .write_mask = 0xfd},
	{CTRL2, "CTRL2"},
	{CTRL3, "CTRL3"},
	{RESERVED_05, "Reserved"},
	/* Bits 7 and 3 are reserved. */
	{CTRL5, "CTRL5", .write_mask = 0x77},
	{RESERVED_07, "Reserved"},
	/* Bits 6 and 3:2 are reserved. */
	{CTRL7, "CTRL7", .write_mask = 0xb3},
	/* Bits 5:4 are reserved. */
	{CTRL8, "CTRL8", .write_mask = 0xcf},
	{CTRL9, "CTRL9"},
	{CAL1_L, "CAL1_L"},
	{CAL1_H, "CAL1_H"},
	{CAL2_L, "CAL2_L"},
	{CAL2_H, "CAL2_H"},
	{CAL3_L, "CAL3_L"},
	{CAL3_H, "CAL3_H"},
	{CAL4_L, "CAL4_L"},
	{CAL4_H, "CAL4_H"},
	{FIFO_WTM_TH, "FIFO_WTM_TH"},
	/* Bits 6:4 are reserved. */
	{FIFO_CTRL, "FIFO_CTRL", .write_mask = 0x8f},
	{FIFO_SMPL_CNT, "FIFO_SMPL_CNT", EMUL_SENSOR_REG_RO},
	{FIFO_STATUS, "FIFO_STATUS", EMUL_SENSOR_REG_RO},
	{FIFO_DATA, "FIFO_DATA", EMUL_SENSOR_REG_RO},
	{STATUSINT, "STATUSINT", EMUL_SENSOR_REG_RO},
	/* gDA and aDA report the updates since the last read of STATUS0. */
	{STATUS0, "STATUS0", EMUL_SENSOR_REG_RO, .clear_on_read = GENMASK(1, 0)},
	{STATUS1, "STATUS1", EMUL_SENSOR_REG_RO},
	{TIMESTAMP_LOW, "TIMESTAMP_LOW", EMUL_SENSOR_REG_RO},
	{TIMESTAMP_MID, "TIMESTAMP_MID", EMUL_SENSOR_REG_RO},
	{TIMESTAMP_HIGH, "TIMESTAMP_HIGH", EMUL_SENSOR_REG_RO},
	{TEMP_L, "TEMP_L", EMUL_SENSOR_REG_RO},
	{TEMP_H, "TEMP_H", EMUL_SENSOR_REG_RO},
	{AX_L, "AX_L", EMUL_SENSOR_REG_RO},
	{AX_H, "AX_H", EMUL_SENSOR_REG_RO},
	{AY_L, "AY_L", EMUL_SENSOR_REG_RO},
	{AY_H, "AY_H", EMUL_SENSOR_REG_RO},
	{AZ_L, "AZ_L", EMUL_SENSOR_REG_RO},
	{AZ_H, "AZ_H", EMUL_SENSOR_REG_RO},
	{GX_L, "GX_L", EMUL_SENSOR_REG_RO},
	{GX_H, "GX_H", EMUL_SENSOR_REG_RO},
	{GY_L, "GY_L", EMUL_SENSOR_REG_RO},
	{GY_H, "GY_H", EMUL_SENSOR_REG_RO},
	{GZ_L, "GZ_L", EMUL_SENSOR_REG_RO},
	{GZ_H, "GZ_H", EMUL_SENSOR_REG_RO},
	{COD_STATUS, "COD_STATUS", EMUL_SENSOR_REG_RO},
	{DQW_L, "dQW_L", EMUL_SENSOR_REG_RO},
	{DQW_H, "dQW_H", EMUL_SENSOR_REG_RO},
	{DQX_L, "dQX_L", EMUL_SENSOR_REG_RO},
	{DQX_H, "dQX_H", EMUL_SENSOR_REG_RO},
	/* Sections 5.9 and 7.4: 0x4d reads 0x80 after a power-on or software reset. */
	{DQY_L, "dQY_L", EMUL_SENSOR_REG_RO, .reset = 0x80},
	{DQY_H, "dQY_H", EMUL_SENSOR_REG_RO},
	{DQZ_L, "dQZ_L", EMUL_SENSOR_REG_RO},
	{DQZ_H, "dQZ_H", EMUL_SENSOR_REG_RO},
	{DVX_L, "dVX_L", EMUL_SENSOR_REG_RO},
	{DVX_H, "dVX_H", EMUL_SENSOR_REG_RO},
	{DVY_L, "dVY_L", EMUL_SENSOR_REG_RO},
	{DVY_H, "dVY_H", EMUL_SENSOR_REG_RO},
	{DVZ_L, "dVZ_L", EMUL_SENSOR_REG_RO},
	{DVZ_H, "dVZ_H", EMUL_SENSOR_REG_RO},
	{TAP_STATUS, "TAP_STATUS", EMUL_SENSOR_REG_RO},
	/* Write only, 0xb0 triggers the soft reset. */
	{RESET, "RESET", .self_clear = 0xff, .reset_on_write = 0xb0},
};

/*
 * 16-bit two's complement value in the _L/_H register pair. CTRL2.aFS<2:0> selects the full
 * scale at 16384, 8192, 4096 and 2048 LSB/g for +-2, +-4, +-8 and +-16 g (table 8); codes 1xx
 * are not defined and keep the +-2 g reset scale.
 */
#define ACCEL(_chan, _reg)                                                                         \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .lsb = G / 16384,                      \
	 .select = {CTRL2, GENMASK(6, 4)},                                                         \
	 .variants = {{.lsb = G / 16384}, {.lsb = G / 8192}, {.lsb = G / 4096},                    \
		      {.lsb = G / 2048}},                                                          \
	 .ready = {STATUS0, BIT(0)},                                                               \
	 .disabled = {.reg = CTRL7, .mask = BIT(0), .value = 0}}

/*
 * CTRL3.gFS<2:0> selects the full scale at 2048, 1024, 512, 256, 128, 64, 32 and 16 LSB/dps for
 * +-16 dps to +-2048 dps (table 9).
 */
#define GYRO(_chan, _reg)                                                                          \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                        \
	 .select = {CTRL3, GENMASK(6, 4)},                                                         \
	 .variants = {{.lsb = DPS / 2048}, {.lsb = DPS / 1024}, {.lsb = DPS / 512},                \
		      {.lsb = DPS / 256}, {.lsb = DPS / 128}, {.lsb = DPS / 64},                   \
		      {.lsb = DPS / 32}, {.lsb = DPS / 16}},                                       \
	 .ready = {STATUS0, BIT(1)},                                                               \
	 .disabled = {.reg = CTRL7, .mask = BIT(1), .value = 0}}

static const struct emul_sensor_channel channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, AX_L),
	ACCEL(SENSOR_CHAN_ACCEL_Y, AY_L),
	ACCEL(SENSOR_CHAN_ACCEL_Z, AZ_L),
	GYRO(SENSOR_CHAN_GYRO_X, GX_L),
	GYRO(SENSOR_CHAN_GYRO_Y, GY_L),
	GYRO(SENSOR_CHAN_GYRO_Z, GZ_L),
	/*
	 * 256 LSB/degC over -40 to 85 degC, section 3.9 and table 19. The temperature sensor
	 * runs whenever the accelerometer or the gyroscope is enabled.
	 */
	{SENSOR_CHAN_DIE_TEMP, .reg = TEMP_L, .is_signed = true, .bits = 16, .lsb = 1.0 / 256,
	 .min = -40.0, .max = 85.0,
	 .disabled = {.reg = CTRL7, .mask = GENMASK(1, 0), .value = 0}},
};

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.increment = {CTRL1, BIT(6)},
	.disabled = {.reg = CTRL1, .mask = BIT(0), .value = BIT(0)},
	.block_update = {.reg = CTRL7, .mask = BIT(7), .value = BIT(7)});
