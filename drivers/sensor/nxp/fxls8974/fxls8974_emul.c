/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_fxls8974

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665

/* FXLS8974CF data sheet Rev. 1.5, section 15, table 20 (reset values for BT_MODE = GND) */
static const struct emul_sensor_reg fxls8974_regs[] = {
	/* SRC_BOOT is set after reset */
	{0x00, "INT_STATUS", EMUL_SENSOR_REG_RO, .reset = 0x01},
	{0x01, "TEMP_OUT", EMUL_SENSOR_REG_RO},
	{0x02, "VECM_LSB", EMUL_SENSOR_REG_RO},
	{0x03, "VECM_MSB", EMUL_SENSOR_REG_RO},
	{0x04, "OUT_X_LSB", EMUL_SENSOR_REG_RO},
	{0x05, "OUT_X_MSB", EMUL_SENSOR_REG_RO},
	{0x06, "OUT_Y_LSB", EMUL_SENSOR_REG_RO},
	{0x07, "OUT_Y_MSB", EMUL_SENSOR_REG_RO},
	{0x08, "OUT_Z_LSB", EMUL_SENSOR_REG_RO},
	{0x09, "OUT_Z_MSB", EMUL_SENSOR_REG_RO},
	{0x0A, "RESERVED_REG1"},
	{0x0B, "BUF_STATUS", EMUL_SENSOR_REG_RO},
	{0x0C, "BUF_X_LSB", EMUL_SENSOR_REG_RO},
	{0x0D, "BUF_X_MSB", EMUL_SENSOR_REG_RO},
	{0x0E, "BUF_Y_LSB", EMUL_SENSOR_REG_RO},
	{0x0F, "BUF_Y_MSB", EMUL_SENSOR_REG_RO},
	{0x10, "BUF_Z_LSB", EMUL_SENSOR_REG_RO},
	{0x11, "BUF_Z_MSB", EMUL_SENSOR_REG_RO},
	/* Revision 1.4 in BCD */
	{0x12, "PROD_REV", EMUL_SENSOR_REG_RO, .reset = 0x14},
	{0x13, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x86},
	{0x14, "SYS_MODE", EMUL_SENSOR_REG_RO},
	/* RST self-clears */
	{0x15, "SENS_CONFIG1", .self_clear = BIT(7)},
	{0x16, "SENS_CONFIG2"},
	{0x17, "SENS_CONFIG3"},
	{0x18, "SENS_CONFIG4", .reset = 0x01},
	{0x19, "SENS_CONFIG5"},
	{0x1A, "WAKE_IDLE_LSB"},
	{0x1B, "WAKE_IDLE_MSB"},
	{0x1C, "SLEEP_IDLE_LSB"},
	{0x1D, "SLEEP_IDLE_MSB"},
	{0x1E, "ASLP_COUNT_LSB"},
	{0x1F, "ASLP_COUNT_MSB"},
	{0x20, "INT_EN"},
	{0x21, "INT_PIN_SEL"},
	{0x22, "OFF_X"},
	{0x23, "OFF_Y"},
	{0x24, "OFF_Z"},
	{0x25, "RESERVED_REG2"},
	{0x26, "BUF_CONFIG1"},
	/* BUF_FLUSH self-clears */
	{0x27, "BUF_CONFIG2", .self_clear = BIT(7)},
	{0x28, "ORIENT_STATUS", EMUL_SENSOR_REG_RO},
	{0x29, "ORIENT_CONFIG", .reset = 0x80},
	{0x2A, "ORIENT_DBCOUNT"},
	{0x2B, "ORIENT_BF_ZCOMP", .reset = 0x44},
	{0x2C, "ORIENT_THS_REG", .reset = 0x84},
	{0x2D, "SDCD_INT_SRC1", EMUL_SENSOR_REG_RO},
	{0x2E, "SDCD_INT_SRC2", EMUL_SENSOR_REG_RO},
	{0x2F, "SDCD_CONFIG1"},
	/* REF_UPD self-clears */
	{0x30, "SDCD_CONFIG2", .self_clear = BIT(0)},
	{0x31, "SDCD_OT_DBCNT"},
	{0x32, "SDCD_WT_DBCNT"},
	{0x33, "SDCD_LTHS_LSB"},
	{0x34, "SDCD_LTHS_MSB"},
	{0x35, "SDCD_UTHS_LSB"},
	{0x36, "SDCD_UTHS_MSB"},
	{0x37, "SELF_TEST_CONFIG1"},
	{0x38, "SELF_TEST_CONFIG2"},
};

/*
 * 12-bit right justified two's complement sample, sign extended to a 16-bit little-endian word
 * (SENS_CONFIG2[LE_BE] = 0, table 28: OUT_x_MSB[7:4] are copies of OUT_x[11]).
 * SENS_CONFIG1[FSR]: 1024, 512, 256, 128 LSB/g; the range is the +-2 g full scale of the reset
 * FSR (section 9). Sets INT_STATUS[SRC_DRDY].
 */
#define ACCEL(_chan, _reg)                                                                         \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .min = -2 * G, .max = 2 * G,           \
	 .select = {0x15, GENMASK(2, 1)},                                                          \
	 .variants = {{.lsb = G / 1024}, {.lsb = G / 512}, {.lsb = G / 256}, {.lsb = G / 128}},    \
	 .ready = {0x00, BIT(7)}}

static const struct emul_sensor_channel fxls8974_channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, 0x04),
	ACCEL(SENSOR_CHAN_ACCEL_Y, 0x06),
	ACCEL(SENSOR_CHAN_ACCEL_Z, 0x08),
	/* 1 degC/LSB, code 0 is 25 degC */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x01, .is_signed = true, .bits = 8, .lsb = 1.0,
	 .offset = 25.0, .min = -40.0, .max = 105.0, .ready = {0x00, BIT(7)}},
};

EMUL_SENSOR_REGMAP_DEFINE(fxls8974_regs, fxls8974_channels);
