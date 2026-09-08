/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lsm6ds0

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * DocID025604 Rev 3, November 2014, sections 6 (register mapping) and 7 (register
 * description): https://www.st.com/resource/en/datasheet/lsm6ds0.pdf
 */
enum {
	ACT_THS = 0x04,
	ACT_DUR = 0x05,
	INT_GEN_CFG_XL = 0x06,
	INT_GEN_THS_X_XL = 0x07,
	INT_GEN_THS_Y_XL = 0x08,
	INT_GEN_THS_Z_XL = 0x09,
	INT_GEN_DUR_XL = 0x0a,
	REFERENCE_G = 0x0b,
	INT_CTRL = 0x0c,
	WHO_AM_I = 0x0f,
	CTRL_REG1_G = 0x10,
	CTRL_REG2_G = 0x11,
	CTRL_REG3_G = 0x12,
	ORIENT_CFG_G = 0x13,
	INT_GEN_SRC_G = 0x14,
	OUT_TEMP_L = 0x15,
	OUT_TEMP_H = 0x16,
	STATUS_REG = 0x17,
	OUT_X_L_G = 0x18,
	OUT_X_H_G = 0x19,
	OUT_Y_L_G = 0x1a,
	OUT_Y_H_G = 0x1b,
	OUT_Z_L_G = 0x1c,
	OUT_Z_H_G = 0x1d,
	CTRL_REG4 = 0x1e,
	CTRL_REG5_XL = 0x1f,
	CTRL_REG6_XL = 0x20,
	CTRL_REG7_XL = 0x21,
	CTRL_REG8 = 0x22,
	CTRL_REG9 = 0x23,
	CTRL_REG10 = 0x24,
	INT_GEN_SRC_XL = 0x26,
	STATUS_REG2 = 0x27,
	OUT_X_L_XL = 0x28,
	OUT_X_H_XL = 0x29,
	OUT_Y_L_XL = 0x2a,
	OUT_Y_H_XL = 0x2b,
	OUT_Z_L_XL = 0x2c,
	OUT_Z_H_XL = 0x2d,
	FIFO_CTRL = 0x2e,
	FIFO_SRC = 0x2f,
	INT_GEN_CFG_G = 0x30,
	INT_GEN_THS_XH_G = 0x31,
	INT_GEN_THS_XL_G = 0x32,
	INT_GEN_THS_YH_G = 0x33,
	INT_GEN_THS_YL_G = 0x34,
	INT_GEN_THS_ZH_G = 0x35,
	INT_GEN_THS_ZL_G = 0x36,
	INT_GEN_DUR_G = 0x37,
};

static const struct emul_sensor_reg registers[] = {
	{0x00, "Reserved", EMUL_SENSOR_REG_RO},
	{0x01, "Reserved", EMUL_SENSOR_REG_RO},
	{0x02, "Reserved", EMUL_SENSOR_REG_RO},
	{0x03, "Reserved", EMUL_SENSOR_REG_RO},
	{ACT_THS, "ACT_THS", .write_mask = 0xff},
	{ACT_DUR, "ACT_DUR", .write_mask = 0xff},
	{INT_GEN_CFG_XL, "INT_GEN_CFG_XL", .write_mask = 0xff},
	{INT_GEN_THS_X_XL, "INT_GEN_THS_X_XL", .write_mask = 0xff},
	{INT_GEN_THS_Y_XL, "INT_GEN_THS_Y_XL", .write_mask = 0xff},
	{INT_GEN_THS_Z_XL, "INT_GEN_THS_Z_XL", .write_mask = 0xff},
	{INT_GEN_DUR_XL, "INT_GEN_DUR_XL", .write_mask = 0xff},
	{REFERENCE_G, "REFERENCE_G", .write_mask = 0xff},
	{INT_CTRL, "INT_CTRL", .write_mask = 0xff},
	{0x0d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x0e, "Reserved", EMUL_SENSOR_REG_RO},
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x68},
	/* Bit 2 must stay 0 (section 7.11) */
	{CTRL_REG1_G, "CTRL_REG1_G", .write_mask = 0xfb},
	/* Bits 7:4 are reserved and must stay 0 (section 7.12) */
	{CTRL_REG2_G, "CTRL_REG2_G", .write_mask = 0x0f},
	/* Bits 5:4 are reserved and must stay 0 (section 7.13) */
	{CTRL_REG3_G, "CTRL_REG3_G", .write_mask = 0xcf},
	/* Bits 7:6 are reserved and must stay 0 (section 7.14) */
	{ORIENT_CFG_G, "ORIENT_CFG_G", .write_mask = 0x3f},
	{INT_GEN_SRC_G, "INT_GEN_SRC_G", EMUL_SENSOR_REG_RO},
	{OUT_TEMP_L, "OUT_TEMP_L", EMUL_SENSOR_REG_RO},
	{OUT_TEMP_H, "OUT_TEMP_H", EMUL_SENSOR_REG_RO},
	{STATUS_REG, "STATUS_REG", EMUL_SENSOR_REG_RO},
	{OUT_X_L_G, "OUT_X_L_G", EMUL_SENSOR_REG_RO},
	{OUT_X_H_G, "OUT_X_H_G", EMUL_SENSOR_REG_RO},
	{OUT_Y_L_G, "OUT_Y_L_G", EMUL_SENSOR_REG_RO},
	{OUT_Y_H_G, "OUT_Y_H_G", EMUL_SENSOR_REG_RO},
	{OUT_Z_L_G, "OUT_Z_L_G", EMUL_SENSOR_REG_RO},
	{OUT_Z_H_G, "OUT_Z_H_G", EMUL_SENSOR_REG_RO},
	/* Bits 7:6 and bit 2 are reserved and must stay 0 (section 7.21) */
	{CTRL_REG4, "CTRL_REG4", .reset = 0x38, .write_mask = 0x3b},
	/* Bits 2:0 are reserved and must stay 0 (section 7.22) */
	{CTRL_REG5_XL, "CTRL_REG5_XL", .reset = 0x38, .write_mask = 0xf8},
	{CTRL_REG6_XL, "CTRL_REG6_XL", .write_mask = 0xff},
	/* Bits 4:3 and bit 1 are reserved and must stay 0 (section 7.24) */
	{CTRL_REG7_XL, "CTRL_REG7_XL", .write_mask = 0xe5},
	/* BOOT (bit 7) is a one-shot trim reload; SW_RESET (bit 0) restores all registers via
	 * write(), which naturally clears it back to its reset value.
	 */
	{CTRL_REG8, "CTRL_REG8", .reset = 0x04, .write_mask = 0xff, .self_clear = BIT(7)},
	/* Bit 7 and bit 5 are reserved and must stay 0 (section 7.26) */
	{CTRL_REG9, "CTRL_REG9", .write_mask = 0x5f},
	/* Only ST_G (bit 2) and ST_XL (bit 0) are writable (section 7.27) */
	{CTRL_REG10, "CTRL_REG10", .write_mask = 0x05},
	{0x25, "Reserved", EMUL_SENSOR_REG_RO},
	{INT_GEN_SRC_XL, "INT_GEN_SRC_XL", EMUL_SENSOR_REG_RO},
	/* Mirror of STATUS_REG (0x17); this model does not keep the two copies in sync */
	{STATUS_REG2, "STATUS_REG", EMUL_SENSOR_REG_RO},
	{OUT_X_L_XL, "OUT_X_L_XL", EMUL_SENSOR_REG_RO},
	{OUT_X_H_XL, "OUT_X_H_XL", EMUL_SENSOR_REG_RO},
	{OUT_Y_L_XL, "OUT_Y_L_XL", EMUL_SENSOR_REG_RO},
	{OUT_Y_H_XL, "OUT_Y_H_XL", EMUL_SENSOR_REG_RO},
	{OUT_Z_L_XL, "OUT_Z_L_XL", EMUL_SENSOR_REG_RO},
	{OUT_Z_H_XL, "OUT_Z_H_XL", EMUL_SENSOR_REG_RO},
	{FIFO_CTRL, "FIFO_CTRL", .write_mask = 0xff},
	{FIFO_SRC, "FIFO_SRC", EMUL_SENSOR_REG_RO},
	{INT_GEN_CFG_G, "INT_GEN_CFG_G", .write_mask = 0xff},
	{INT_GEN_THS_XH_G, "INT_GEN_THS_XH_G", .write_mask = 0xff},
	{INT_GEN_THS_XL_G, "INT_GEN_THS_XL_G", .write_mask = 0xff},
	/* Bit 7 is reserved and must stay 0 (section 7.37); unlike the X register, this high
	 * byte has no DCRM_G bit.
	 */
	{INT_GEN_THS_YH_G, "INT_GEN_THS_YH_G", .write_mask = 0x7f},
	{INT_GEN_THS_YL_G, "INT_GEN_THS_YL_G", .write_mask = 0xff},
	{INT_GEN_THS_ZH_G, "INT_GEN_THS_ZH_G", .write_mask = 0x7f},
	{INT_GEN_THS_ZL_G, "INT_GEN_THS_ZL_G", .write_mask = 0xff},
	{INT_GEN_DUR_G, "INT_GEN_DUR_G", .write_mask = 0xff},
	/* Reserved (section 6); writing here may permanently damage the device */
	{0x38, "Reserved", EMUL_SENSOR_REG_RO},
	{0x39, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x3f, "Reserved", EMUL_SENSOR_REG_RO},
	{0x40, "Reserved", EMUL_SENSOR_REG_RO},
	{0x41, "Reserved", EMUL_SENSOR_REG_RO},
	{0x42, "Reserved", EMUL_SENSOR_REG_RO},
	{0x43, "Reserved", EMUL_SENSOR_REG_RO},
	{0x44, "Reserved", EMUL_SENSOR_REG_RO},
	{0x45, "Reserved", EMUL_SENSOR_REG_RO},
	{0x46, "Reserved", EMUL_SENSOR_REG_RO},
	{0x47, "Reserved", EMUL_SENSOR_REG_RO},
	{0x48, "Reserved", EMUL_SENSOR_REG_RO},
	{0x49, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x4f, "Reserved", EMUL_SENSOR_REG_RO},
	{0x50, "Reserved", EMUL_SENSOR_REG_RO},
	{0x51, "Reserved", EMUL_SENSOR_REG_RO},
	{0x52, "Reserved", EMUL_SENSOR_REG_RO},
	{0x53, "Reserved", EMUL_SENSOR_REG_RO},
	{0x54, "Reserved", EMUL_SENSOR_REG_RO},
	{0x55, "Reserved", EMUL_SENSOR_REG_RO},
	{0x56, "Reserved", EMUL_SENSOR_REG_RO},
	{0x57, "Reserved", EMUL_SENSOR_REG_RO},
	{0x58, "Reserved", EMUL_SENSOR_REG_RO},
	{0x59, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x5f, "Reserved", EMUL_SENSOR_REG_RO},
	{0x60, "Reserved", EMUL_SENSOR_REG_RO},
	{0x61, "Reserved", EMUL_SENSOR_REG_RO},
	{0x62, "Reserved", EMUL_SENSOR_REG_RO},
	{0x63, "Reserved", EMUL_SENSOR_REG_RO},
	{0x64, "Reserved", EMUL_SENSOR_REG_RO},
	{0x65, "Reserved", EMUL_SENSOR_REG_RO},
	{0x66, "Reserved", EMUL_SENSOR_REG_RO},
	{0x67, "Reserved", EMUL_SENSOR_REG_RO},
	{0x68, "Reserved", EMUL_SENSOR_REG_RO},
	{0x69, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x6f, "Reserved", EMUL_SENSOR_REG_RO},
	{0x70, "Reserved", EMUL_SENSOR_REG_RO},
	{0x71, "Reserved", EMUL_SENSOR_REG_RO},
	{0x72, "Reserved", EMUL_SENSOR_REG_RO},
	{0x73, "Reserved", EMUL_SENSOR_REG_RO},
	{0x74, "Reserved", EMUL_SENSOR_REG_RO},
	{0x75, "Reserved", EMUL_SENSOR_REG_RO},
	{0x76, "Reserved", EMUL_SENSOR_REG_RO},
	{0x77, "Reserved", EMUL_SENSOR_REG_RO},
	{0x78, "Reserved", EMUL_SENSOR_REG_RO},
	{0x79, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x7f, "Reserved", EMUL_SENSOR_REG_RO},
};

#define G 9.80665
/* Radians per degree, used to convert the datasheet's mdps/LSB to rad/s/LSB */
#define RAD_PER_DEG (3.14159265358979323846 / 180.0)

/*
 * 16-bit two's complement per axis (sections 7.30-7.32). FS1_XL:FS0_XL select the full scale in
 * a non-monotonic order (section 7.23): 00 -> 2g, 01 -> 16g, 10 -> 4g, 11 -> 8g (Table 63).
 * XLDA (STATUS_REG bit 0) flags new data. The accelerometer converts whenever ODR_XL is nonzero
 * (Table 64); this model does not additionally track the datasheet's documented alternative path
 * of enabling the accelerometer solely by activating the gyroscope through CTRL_REG1_G while
 * ODR_XL stays at its power-down reset value (section 3.1, Figure 5).
 */
#define ACCEL(_chan, _reg)                                                                      \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                     \
	 .select = {CTRL_REG6_XL, GENMASK(4, 3)},                                               \
	 .variants = {{.lsb = 0.061e-3 * G}, {.lsb = 0.732e-3 * G},                              \
		      {.lsb = 0.122e-3 * G}, {.lsb = 0.244e-3 * G}},                             \
	 .ready = {STATUS_REG, BIT(0)},                                                          \
	 .disabled = {CTRL_REG6_XL, GENMASK(7, 5), 0}}

/*
 * 16-bit two's complement per axis (sections 7.18-7.20). FS_G1:FS_G0 select the full scale
 * (section 7.11, Table 41): 00 -> 245 dps, 01 -> 500 dps, 10 -> not available, 11 -> 2000 dps.
 * GDA (STATUS_REG bit 1) flags new data. Disabled while ODR_G is power-down (Table 9).
 */
#define GYRO(_chan, _reg)                                                                       \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                     \
	 .select = {CTRL_REG1_G, GENMASK(4, 3)},                                                \
	 .variants = {{.lsb = 8.75e-3 * RAD_PER_DEG}, {.lsb = 17.50e-3 * RAD_PER_DEG}, {0},      \
		      {.lsb = 70e-3 * RAD_PER_DEG}},                                             \
	 .ready = {STATUS_REG, BIT(1)},                                                          \
	 .disabled = {CTRL_REG1_G, GENMASK(7, 5), 0}}

static const struct emul_sensor_channel channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, OUT_X_L_XL),
	ACCEL(SENSOR_CHAN_ACCEL_Y, OUT_Y_L_XL),
	ACCEL(SENSOR_CHAN_ACCEL_Z, OUT_Z_L_XL),
	GYRO(SENSOR_CHAN_GYRO_X, OUT_X_L_G),
	GYRO(SENSOR_CHAN_GYRO_Y, OUT_Y_L_G),
	GYRO(SENSOR_CHAN_GYRO_Z, OUT_Z_L_G),
	/*
	 * 16-bit two's complement (section 7.16); only the low 12 bits carry data, sign extended
	 * into the high byte's top 4 bits (Table 54), so a plain 16-bit signed field decodes to
	 * the same value. 16 LSB/degC, 0 LSB (typ.) at 25 degC (Table 5). TDA (STATUS_REG bit 2)
	 * flags new data.
	 */
	{SENSOR_CHAN_DIE_TEMP, .reg = OUT_TEMP_L, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 16, .offset = 25.0, .min = -40.0, .max = 85.0,
	 .ready = {STATUS_REG, BIT(2)}},
};

/* SW_RESET (CTRL_REG8 bit 0) restores every register to its reset value, which also clears the
 * bit itself since CTRL_REG8's own reset value has it at 0.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == CTRL_REG8 && (data->regs[CTRL_REG8] & BIT(0)) != 0U) {
		emul_sensor_regmap_reset(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	/* IF_ADD_INC (CTRL_REG8 bit 2), enabled by reset default 0x04 (section 7.25) */
	.increment = {CTRL_REG8, BIT(2)},
	/* BDU (CTRL_REG8 bit 6): hold output registers until fully read (section 7.25) */
	.block_update = {CTRL_REG8, BIT(6), BIT(6)},
	.write = write);
