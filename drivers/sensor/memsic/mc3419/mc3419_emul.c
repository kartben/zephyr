/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT memsic_mc3419

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665

/* MC3419 datasheet APS-048-0071 v1.2, section 12.1 table 23; 8-bit registers, low byte first */
static const struct emul_sensor_reg mc3419_regs[] = {
	{0x05, "DEV_STAT", EMUL_SENSOR_REG_RO},
	{0x06, "INTR_CTRL"},
	{0x07, "MODE"},
	{0x08, "SR"},
	{0x09, "MOTION_CTRL"},
	/* FIFO_EMPTY is set at POR (section 12.7) */
	{0x0A, "FIFO_STAT", EMUL_SENSOR_REG_RO, .reset = 0x01},
	{0x0B, "FIFO_RD_P", EMUL_SENSOR_REG_RO},
	{0x0C, "FIFO_WR_P", EMUL_SENSOR_REG_RO},
	{0x0D, "XOUT_EX_L", EMUL_SENSOR_REG_RO},
	{0x0E, "XOUT_EX_H", EMUL_SENSOR_REG_RO},
	{0x0F, "YOUT_EX_L", EMUL_SENSOR_REG_RO},
	{0x10, "YOUT_EX_H", EMUL_SENSOR_REG_RO},
	{0x11, "ZOUT_EX_L", EMUL_SENSOR_REG_RO},
	{0x12, "ZOUT_EX_H", EMUL_SENSOR_REG_RO},
	{0x13, "STATUS", EMUL_SENSOR_REG_RO},
	{0x14, "INTR_STAT"},
	{0x18, "CHIP_ID", EMUL_SENSOR_REG_RO, .reset = 0xA4},
	{0x20, "RANGE"},
	{0x21, "XOFFL"},
	{0x22, "XOFFH"},
	{0x23, "YOFFL"},
	{0x24, "YOFFH"},
	{0x25, "ZOFFL"},
	{0x26, "ZOFFH"},
	{0x27, "XGAIN"},
	{0x28, "YGAIN"},
	{0x29, "ZGAIN"},
	{0x2D, "FIFO_CTRL"},
	{0x2E, "FIFO_TH", .reset = 0x10},
	{0x2F, "FIFO_INTR", EMUL_SENSOR_REG_RO},
	{0x30, "FIFO_CTRL2_SR2"},
	{0x31, "COMM_CTRL"},
	{0x33, "GPIO_CTRL"},
	{0x40, "TF_THRESH_LSB"},
	{0x41, "TF_THRESH_MSB"},
	{0x42, "TF_DB"},
	{0x43, "AM_THRESH_LSB"},
	{0x44, "AM_THRESH_MSB"},
	{0x45, "AM_DB"},
	{0x46, "SHK_THRESH_LSB"},
	{0x47, "SHK_THRESH_MSB"},
	{0x48, "PK_P2P_DUR_THRESH_LSB"},
	{0x49, "PK_P2P_DUR_THRESH_MSB"},
	{0x4A, "TIMER_CTRL"},
	{0x4B, "RD_CNT", .reset = 0x06},
};

/*
 * RANGE[2:0]: 000 +/-2g 16384, 001 +/-4g 8192, 010 +/-8g 4096, 011 +/-16g 2048,
 * 100 +/-12g 2730 LSB/g (tables 5 and 35); reserved codes keep the +/-2g default.
 * NEW_DATA in STATUS is set with every sample.
 */
#define ACCEL(_chan, _reg)                                                                         \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .lsb = G / 16384,                      \
	 .select = {0x20, GENMASK(6, 4)},                                                          \
	 .variants = {{.lsb = G / 16384}, {.lsb = G / 8192}, {.lsb = G / 4096}, {.lsb = G / 2048}, \
		      {.lsb = G / 2730}},                                                          \
	 .ready = {0x13, BIT(7)}}

static const struct emul_sensor_channel mc3419_channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, 0x0D),
	ACCEL(SENSOR_CHAN_ACCEL_Y, 0x0F),
	ACCEL(SENSOR_CHAN_ACCEL_Z, 0x11),
};

EMUL_SENSOR_REGMAP_DEFINE(mc3419_regs, mc3419_channels);
