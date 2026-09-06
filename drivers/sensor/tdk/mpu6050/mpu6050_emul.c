/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT invensense_mpu6050

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G   9.80665
#define DPS (3.14159265358979 / 180.0)

/* MPU-6000/MPU-6050 register map RM-MPU-6000A-00 rev 4.0, section 3 */
static const struct emul_sensor_reg mpu6050_regs[] = {
	{0x0D, "SELF_TEST_X"},
	{0x0E, "SELF_TEST_Y"},
	{0x0F, "SELF_TEST_Z"},
	{0x10, "SELF_TEST_A"},
	{0x19, "SMPLRT_DIV"},
	{0x1A, "CONFIG"},
	{0x1B, "GYRO_CONFIG"},
	{0x1C, "ACCEL_CONFIG"},
	{0x1F, "MOT_THR"},
	{0x23, "FIFO_EN"},
	{0x24, "I2C_MST_CTRL"},
	{0x36, "I2C_MST_STATUS", EMUL_SENSOR_REG_RO},
	{0x37, "INT_PIN_CFG"},
	{0x38, "INT_ENABLE"},
	{0x3A, "INT_STATUS", EMUL_SENSOR_REG_RO},
	{0x3B, "ACCEL_XOUT_H", EMUL_SENSOR_REG_RO},
	{0x3C, "ACCEL_XOUT_L", EMUL_SENSOR_REG_RO},
	{0x3D, "ACCEL_YOUT_H", EMUL_SENSOR_REG_RO},
	{0x3E, "ACCEL_YOUT_L", EMUL_SENSOR_REG_RO},
	{0x3F, "ACCEL_ZOUT_H", EMUL_SENSOR_REG_RO},
	{0x40, "ACCEL_ZOUT_L", EMUL_SENSOR_REG_RO},
	{0x41, "TEMP_OUT_H", EMUL_SENSOR_REG_RO},
	{0x42, "TEMP_OUT_L", EMUL_SENSOR_REG_RO},
	{0x43, "GYRO_XOUT_H", EMUL_SENSOR_REG_RO},
	{0x44, "GYRO_XOUT_L", EMUL_SENSOR_REG_RO},
	{0x45, "GYRO_YOUT_H", EMUL_SENSOR_REG_RO},
	{0x46, "GYRO_YOUT_L", EMUL_SENSOR_REG_RO},
	{0x47, "GYRO_ZOUT_H", EMUL_SENSOR_REG_RO},
	{0x48, "GYRO_ZOUT_L", EMUL_SENSOR_REG_RO},
	{0x68, "SIGNAL_PATH_RESET"},
	{0x69, "MOT_DETECT_CTRL"},
	{0x6A, "USER_CTRL"},
	/* Resets with SLEEP set, DEVICE_RESET self-clears */
	{0x6B, "PWR_MGMT_1", .reset = 0x40, .self_clear = BIT(7)},
	{0x6C, "PWR_MGMT_2"},
	{0x72, "FIFO_COUNTH"},
	{0x73, "FIFO_COUNTL"},
	{0x74, "FIFO_R_W"},
	{0x75, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x68},
};

/* AFS_SEL: 16384, 8192, 4096, 2048 LSB/g */
#define ACCEL(_chan, _reg)                                                                         \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .select = {0x1C, GENMASK(4, 3)},       \
	 .variants = {{.lsb = G / 16384}, {.lsb = G / 8192}, {.lsb = G / 4096}, {.lsb = G / 2048}}}

/* FS_SEL: 131, 65.5, 32.8, 16.4 LSB/(deg/s) */
#define GYRO(_chan, _reg)                                                                          \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .select = {0x1B, GENMASK(4, 3)},       \
	 .variants = {{.lsb = DPS / 131}, {.lsb = DPS / 65.5}, {.lsb = DPS / 32.8},                \
		      {.lsb = DPS / 16.4}}}

static const struct emul_sensor_channel mpu6050_channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, 0x3B),
	ACCEL(SENSOR_CHAN_ACCEL_Y, 0x3D),
	ACCEL(SENSOR_CHAN_ACCEL_Z, 0x3F),
	/* degC = TEMP_OUT / 340 + 36.53 */
	{SENSOR_CHAN_DIE_TEMP, .reg = 0x41, .is_signed = true, .bits = 16, .lsb = 1.0 / 340,
	 .offset = 36.53, .min = -40.0, .max = 85.0},
	GYRO(SENSOR_CHAN_GYRO_X, 0x43),
	GYRO(SENSOR_CHAN_GYRO_Y, 0x45),
	GYRO(SENSOR_CHAN_GYRO_Z, 0x47),
};

EMUL_SENSOR_REGMAP_DEFINE(mpu6050_regs, mpu6050_channels, .big_endian = true);
