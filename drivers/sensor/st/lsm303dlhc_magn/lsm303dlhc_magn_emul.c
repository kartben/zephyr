/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lsm303dlhc_magn

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * LSM303DLHC Doc ID 018771 Rev 1, section 6 (register mapping) and section 7.2 (magnetic field
 * sensing register description): https://www.st.com/resource/en/datasheet/lsm303dlhc.pdf
 *
 * The magnetometer core answers on its own 7-bit slave address (0011110b) and register map,
 * distinct from the accelerometer core covered by the "st,lsm303dlhc-accel" compatible.
 */
enum {
	CRA_REG_M = 0x00,
	CRB_REG_M = 0x01,
	MR_REG_M = 0x02,
	OUT_X_H_M = 0x03,
	OUT_X_L_M = 0x04,
	OUT_Z_H_M = 0x05,
	OUT_Z_L_M = 0x06,
	OUT_Y_H_M = 0x07,
	OUT_Y_L_M = 0x08,
	/* Named SR_REG_M in the section heading, SR_REG_Mg in the register address map. */
	SR_REG_M = 0x09,
	IRA_REG_M = 0x0a,
	IRB_REG_M = 0x0b,
	IRC_REG_M = 0x0c,
	TEMP_OUT_H_M = 0x31,
	TEMP_OUT_L_M = 0x32,
};

static const struct emul_sensor_reg registers[] = {
	/* TEMP_EN and DO2:0 are writable; the remaining bits must stay 0. */
	{CRA_REG_M, "CRA_REG_M", .reset = 0x10, .write_mask = BIT(7) | GENMASK(4, 2)},
	/* GN2:0 are writable; the remaining bits must stay 0. */
	{CRB_REG_M, "CRB_REG_M", .reset = 0x20, .write_mask = GENMASK(7, 5)},
	/* MD1:0 are writable; the remaining bits must stay 0. Powers up in sleep-mode. */
	{MR_REG_M, "MR_REG_M", .reset = 0x03, .write_mask = GENMASK(1, 0)},
	{OUT_X_H_M, "OUT_X_H_M", EMUL_SENSOR_REG_RO},
	{OUT_X_L_M, "OUT_X_L_M", EMUL_SENSOR_REG_RO},
	{OUT_Z_H_M, "OUT_Z_H_M", EMUL_SENSOR_REG_RO},
	{OUT_Z_L_M, "OUT_Z_L_M", EMUL_SENSOR_REG_RO},
	{OUT_Y_H_M, "OUT_Y_H_M", EMUL_SENSOR_REG_RO},
	{OUT_Y_L_M, "OUT_Y_L_M", EMUL_SENSOR_REG_RO},
	{SR_REG_M, "SR_REG_M", EMUL_SENSOR_REG_RO},
	{IRA_REG_M, "IRA_REG_M", EMUL_SENSOR_REG_RO, .reset = 0x48},
	{IRB_REG_M, "IRB_REG_M", EMUL_SENSOR_REG_RO, .reset = 0x34},
	{IRC_REG_M, "IRC_REG_M", EMUL_SENSOR_REG_RO, .reset = 0x33},
	/*
	 * 0x0D-0x30: "Reserved (do not modify)". The device answers on these addresses (they
	 * hold factory trim data restored at power-up) but their power-on content is not given
	 * numerically, so they are modeled read-only, reading 0.
	 */
	{0x0d, "reserved", EMUL_SENSOR_REG_RO},
	{0x0e, "reserved", EMUL_SENSOR_REG_RO},
	{0x0f, "reserved", EMUL_SENSOR_REG_RO},
	{0x10, "reserved", EMUL_SENSOR_REG_RO},
	{0x11, "reserved", EMUL_SENSOR_REG_RO},
	{0x12, "reserved", EMUL_SENSOR_REG_RO},
	{0x13, "reserved", EMUL_SENSOR_REG_RO},
	{0x14, "reserved", EMUL_SENSOR_REG_RO},
	{0x15, "reserved", EMUL_SENSOR_REG_RO},
	{0x16, "reserved", EMUL_SENSOR_REG_RO},
	{0x17, "reserved", EMUL_SENSOR_REG_RO},
	{0x18, "reserved", EMUL_SENSOR_REG_RO},
	{0x19, "reserved", EMUL_SENSOR_REG_RO},
	{0x1a, "reserved", EMUL_SENSOR_REG_RO},
	{0x1b, "reserved", EMUL_SENSOR_REG_RO},
	{0x1c, "reserved", EMUL_SENSOR_REG_RO},
	{0x1d, "reserved", EMUL_SENSOR_REG_RO},
	{0x1e, "reserved", EMUL_SENSOR_REG_RO},
	{0x1f, "reserved", EMUL_SENSOR_REG_RO},
	{0x20, "reserved", EMUL_SENSOR_REG_RO},
	{0x21, "reserved", EMUL_SENSOR_REG_RO},
	{0x22, "reserved", EMUL_SENSOR_REG_RO},
	{0x23, "reserved", EMUL_SENSOR_REG_RO},
	{0x24, "reserved", EMUL_SENSOR_REG_RO},
	{0x25, "reserved", EMUL_SENSOR_REG_RO},
	{0x26, "reserved", EMUL_SENSOR_REG_RO},
	{0x27, "reserved", EMUL_SENSOR_REG_RO},
	{0x28, "reserved", EMUL_SENSOR_REG_RO},
	{0x29, "reserved", EMUL_SENSOR_REG_RO},
	{0x2a, "reserved", EMUL_SENSOR_REG_RO},
	{0x2b, "reserved", EMUL_SENSOR_REG_RO},
	{0x2c, "reserved", EMUL_SENSOR_REG_RO},
	{0x2d, "reserved", EMUL_SENSOR_REG_RO},
	{0x2e, "reserved", EMUL_SENSOR_REG_RO},
	{0x2f, "reserved", EMUL_SENSOR_REG_RO},
	{0x30, "reserved", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_H_M, "TEMP_OUT_H_M", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_L_M, "TEMP_OUT_L_M", EMUL_SENSOR_REG_RO},
	/* 0x33-0x3A: "Reserved (do not modify)", same rationale as 0x0D-0x30. */
	{0x33, "reserved", EMUL_SENSOR_REG_RO},
	{0x34, "reserved", EMUL_SENSOR_REG_RO},
	{0x35, "reserved", EMUL_SENSOR_REG_RO},
	{0x36, "reserved", EMUL_SENSOR_REG_RO},
	{0x37, "reserved", EMUL_SENSOR_REG_RO},
	{0x38, "reserved", EMUL_SENSOR_REG_RO},
	{0x39, "reserved", EMUL_SENSOR_REG_RO},
	{0x3a, "reserved", EMUL_SENSOR_REG_RO},
};

/*
 * 16-bit two's complement value, big-endian (high byte first), common to X, Y and Z (Table 84 in
 * section 7.2.4-7.2.6; sections 7.2.4-7.2.6 give the register pairs, Table 3/Table 75 give the
 * full-scale range and gain). GN2:0 in CRB_REG_M (bits 7:5) selects the gain, common to all
 * three axes but with a different LSB/gauss for Z than for X and Y (Table 75). GN = 000 is not
 * listed in Table 75; variant 0 below duplicates the GN = 001 (reset) entry as there is no
 * documented behavior for that code.
 */
#define MAGN_XY(_chan, _reg)                                                                    \
	{.chan = _chan, .reg = _reg, .is_signed = true, .bits = 16,                              \
	 .select = {CRB_REG_M, GENMASK(7, 5)},                                                    \
	 .variants = {{.lsb = 1.0 / 1100, .min = -1.3, .max = 1.3},                               \
		      {.lsb = 1.0 / 1100, .min = -1.3, .max = 1.3},                               \
		      {.lsb = 1.0 / 855, .min = -1.9, .max = 1.9},                                \
		      {.lsb = 1.0 / 670, .min = -2.5, .max = 2.5},                                \
		      {.lsb = 1.0 / 450, .min = -4.0, .max = 4.0},                                \
		      {.lsb = 1.0 / 400, .min = -4.7, .max = 4.7},                                \
		      {.lsb = 1.0 / 330, .min = -5.6, .max = 5.6},                                \
		      {.lsb = 1.0 / 230, .min = -8.1, .max = 8.1}},                               \
	 .ready = {SR_REG_M, BIT(0)}}

#define MAGN_Z(_chan, _reg)                                                                      \
	{.chan = _chan, .reg = _reg, .is_signed = true, .bits = 16,                              \
	 .select = {CRB_REG_M, GENMASK(7, 5)},                                                    \
	 .variants = {{.lsb = 1.0 / 980, .min = -1.3, .max = 1.3},                                \
		      {.lsb = 1.0 / 980, .min = -1.3, .max = 1.3},                                \
		      {.lsb = 1.0 / 760, .min = -1.9, .max = 1.9},                                \
		      {.lsb = 1.0 / 600, .min = -2.5, .max = 2.5},                                \
		      {.lsb = 1.0 / 400, .min = -4.0, .max = 4.0},                                \
		      {.lsb = 1.0 / 355, .min = -4.7, .max = 4.7},                                \
		      {.lsb = 1.0 / 295, .min = -5.6, .max = 5.6},                                \
		      {.lsb = 1.0 / 205, .min = -8.1, .max = 8.1}},                               \
	 .ready = {SR_REG_M, BIT(0)}}

static const struct emul_sensor_channel channels[] = {
	MAGN_XY(SENSOR_CHAN_MAGN_X, OUT_X_H_M),
	MAGN_XY(SENSOR_CHAN_MAGN_Y, OUT_Y_H_M),
	MAGN_Z(SENSOR_CHAN_MAGN_Z, OUT_Z_H_M),
	/*
	 * Table 84/85/86: TEMP11:4 in the high byte, TEMP3:0 in bits 7:4 of the low byte, bits
	 * 3:0 of the low byte unused; 8 LSB/degC (0.125 degC/LSB), 12-bit two's complement.
	 * TEMP_EN in CRA_REG_M enables the sensor; the datasheet gives no absolute calibration
	 * point, so min/max follow the module's operating temperature range (Table 4/Table 7)
	 * rather than the full 12-bit code range.
	 */
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT_H_M, .is_signed = true, .bits = 12,
	 .pos = 4, .lsb = 0.125, .min = -40, .max = 85,
	 .disabled = {.reg = CRA_REG_M, .mask = BIT(7), .value = 0}},
};

/*
 * MD1:0 = 10 or 11 places the device in sleep-mode (Table 78); it does not convert while in
 * that state. Continuous-conversion (00) and single-conversion (01) mode both leave MD1 clear.
 * Like other parts that only convert while not disabled, exiting sleep-mode does not by itself
 * re-run the retained inputs through the enabled configuration (they get applied only when a
 * new input is injected), so convert them explicitly on the MD1 1->0 transition.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == MR_REG_M && (old & BIT(1)) != 0U && (data->regs[MR_REG_M] & BIT(1)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.disabled = {.reg = MR_REG_M, .mask = BIT(1), .value = BIT(1)}, .write = write);
