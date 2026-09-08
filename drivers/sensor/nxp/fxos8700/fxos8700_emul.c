/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_fxos8700

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665

/*
 * FXOS8700CQ, 6-axis sensor with integrated linear accelerometer and magnetometer.
 * NXP document FXOS8700CQ, Rev. 8.0, 25 April 2017, Section 14 "Register descriptions",
 * Table 16 "Register address map": https://www.nxp.com/docs/en/data-sheet/FXOS8700CQ.pdf
 *
 * Only the registers that select operating mode, full-scale range, and the accelerometer,
 * magnetometer and temperature outputs are modeled in detail. The embedded event-detection
 * blocks (freefall/motion, transient, pulse/tap, landscape/portrait, vector-magnitude,
 * magnetic threshold, magnetometer auto-calibration min/max, FIFO) are listed with their
 * address, name and reset value for completeness but their bit-level behavior is not
 * simulated; see the notes below the register table.
 */
enum {
	STATUS = 0x00,
	OUT_X_MSB = 0x01,
	OUT_X_LSB = 0x02,
	OUT_Y_MSB = 0x03,
	OUT_Y_LSB = 0x04,
	OUT_Z_MSB = 0x05,
	OUT_Z_LSB = 0x06,
	F_SETUP = 0x09,
	TRIG_CFG = 0x0A,
	SYSMOD = 0x0B,
	INT_SOURCE = 0x0C,
	WHO_AM_I = 0x0D,
	XYZ_DATA_CFG = 0x0E,
	HP_FILTER_CUTOFF = 0x0F,
	PL_STATUS = 0x10,
	PL_CFG = 0x11,
	PL_COUNT = 0x12,
	PL_BF_ZCOMP = 0x13,
	PL_THS_REG = 0x14,
	A_FFMT_CFG = 0x15,
	A_FFMT_SRC = 0x16,
	A_FFMT_THS = 0x17,
	A_FFMT_COUNT = 0x18,
	TRANSIENT_CFG = 0x1D,
	TRANSIENT_SRC = 0x1E,
	TRANSIENT_THS = 0x1F,
	TRANSIENT_COUNT = 0x20,
	PULSE_CFG = 0x21,
	PULSE_SRC = 0x22,
	PULSE_THSX = 0x23,
	PULSE_THSY = 0x24,
	PULSE_THSZ = 0x25,
	PULSE_TMLT = 0x26,
	PULSE_LTCY = 0x27,
	PULSE_WIND = 0x28,
	ASLP_COUNT = 0x29,
	CTRL_REG1 = 0x2A,
	CTRL_REG2 = 0x2B,
	CTRL_REG3 = 0x2C,
	CTRL_REG4 = 0x2D,
	CTRL_REG5 = 0x2E,
	OFF_X = 0x2F,
	OFF_Y = 0x30,
	OFF_Z = 0x31,
	M_DR_STATUS = 0x32,
	M_OUT_X_MSB = 0x33,
	M_OUT_X_LSB = 0x34,
	M_OUT_Y_MSB = 0x35,
	M_OUT_Y_LSB = 0x36,
	M_OUT_Z_MSB = 0x37,
	M_OUT_Z_LSB = 0x38,
	CMP_X_MSB = 0x39,
	CMP_X_LSB = 0x3A,
	CMP_Y_MSB = 0x3B,
	CMP_Y_LSB = 0x3C,
	CMP_Z_MSB = 0x3D,
	CMP_Z_LSB = 0x3E,
	M_OFF_X_MSB = 0x3F,
	M_OFF_X_LSB = 0x40,
	M_OFF_Y_MSB = 0x41,
	M_OFF_Y_LSB = 0x42,
	M_OFF_Z_MSB = 0x43,
	M_OFF_Z_LSB = 0x44,
	MAX_X_MSB = 0x45,
	MAX_X_LSB = 0x46,
	MAX_Y_MSB = 0x47,
	MAX_Y_LSB = 0x48,
	MAX_Z_MSB = 0x49,
	MAX_Z_LSB = 0x4A,
	MIN_X_MSB = 0x4B,
	MIN_X_LSB = 0x4C,
	MIN_Y_MSB = 0x4D,
	MIN_Y_LSB = 0x4E,
	MIN_Z_MSB = 0x4F,
	MIN_Z_LSB = 0x50,
	TEMP = 0x51,
	M_THS_CFG = 0x52,
	M_THS_SRC = 0x53,
	M_THS_X_MSB = 0x54,
	M_THS_X_LSB = 0x55,
	M_THS_Y_MSB = 0x56,
	M_THS_Y_LSB = 0x57,
	M_THS_Z_MSB = 0x58,
	M_THS_Z_LSB = 0x59,
	M_THS_COUNT = 0x5A,
	M_CTRL_REG1 = 0x5B,
	M_CTRL_REG2 = 0x5C,
	M_CTRL_REG3 = 0x5D,
	M_INT_SRC = 0x5E,
	A_VECM_CFG = 0x5F,
	A_VECM_THS_MSB = 0x60,
	A_VECM_THS_LSB = 0x61,
	A_VECM_CNT = 0x62,
	A_VECM_INITX_MSB = 0x63,
	A_VECM_INITX_LSB = 0x64,
	A_VECM_INITY_MSB = 0x65,
	A_VECM_INITY_LSB = 0x66,
	A_VECM_INITZ_MSB = 0x67,
	A_VECM_INITZ_LSB = 0x68,
	M_VECM_CFG = 0x69,
	M_VECM_THS_MSB = 0x6A,
	M_VECM_THS_LSB = 0x6B,
	M_VECM_CNT = 0x6C,
	M_VECM_INITX_MSB = 0x6D,
	M_VECM_INITX_LSB = 0x6E,
	M_VECM_INITY_MSB = 0x6F,
	M_VECM_INITY_LSB = 0x70,
	M_VECM_INITZ_MSB = 0x71,
	M_VECM_INITZ_LSB = 0x72,
	A_FFMT_THS_X_MSB = 0x73,
	A_FFMT_THS_X_LSB = 0x74,
	A_FFMT_THS_Y_MSB = 0x75,
	A_FFMT_THS_Y_LSB = 0x76,
	A_FFMT_THS_Z_MSB = 0x77,
	A_FFMT_THS_Z_LSB = 0x78,
};

/*
 * DR_STATUS bit allocation (register 0x00 when F_SETUP[f_mode] = 0, Table 20) and
 * M_DR_STATUS bit allocation (register 0x32, Table 151) share the same layout:
 * bit7 zyxow/xyzow, bit6 zow, bit5 yow, bit4 xow, bit3 zyxdr/xyzdr, bit2 zdr, bit1 ydr,
 * bit0 xdr. Each axis' overwrite bit sits 4 positions above its data-ready bit.
 */
#define DR_XDR   BIT(0)
#define DR_YDR   BIT(1)
#define DR_ZDR   BIT(2)
#define DR_ZYXDR BIT(3)

static const struct emul_sensor_reg regs[] = {
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO},
	/* 14-bit left-justified sample data, Section 14.4.1 */
	{OUT_X_MSB, "OUT_X_MSB", EMUL_SENSOR_REG_RO, .read_clears = {STATUS, DR_XDR}},
	{OUT_X_LSB, "OUT_X_LSB", EMUL_SENSOR_REG_RO},
	{OUT_Y_MSB, "OUT_Y_MSB", EMUL_SENSOR_REG_RO, .read_clears = {STATUS, DR_YDR}},
	{OUT_Y_LSB, "OUT_Y_LSB", EMUL_SENSOR_REG_RO},
	/* Z is read last in the X,Y,Z burst; clears zyxdr along with zdr (Table 21) */
	{OUT_Z_MSB, "OUT_Z_MSB", EMUL_SENSOR_REG_RO, .read_clears = {STATUS, DR_ZDR | DR_ZYXDR}},
	{OUT_Z_LSB, "OUT_Z_LSB", EMUL_SENSOR_REG_RO},
	{0x07, "Reserved", EMUL_SENSOR_REG_RO},
	{0x08, "Reserved", EMUL_SENSOR_REG_RO},
	{F_SETUP, "F_SETUP"},
	{TRIG_CFG, "TRIG_CFG"},
	{SYSMOD, "SYSMOD", EMUL_SENSOR_REG_RO},
	{INT_SOURCE, "INT_SOURCE", EMUL_SENSOR_REG_RO},
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0xC7},
	/* bit4 hpf_out, bits3:2 reserved, bits1:0 fs[1:0] (Table 59) */
	{XYZ_DATA_CFG, "XYZ_DATA_CFG", .write_mask = BIT(4) | GENMASK(1, 0)},
	{HP_FILTER_CUTOFF, "HP_FILTER_CUTOFF"},
	{PL_STATUS, "PL_STATUS", EMUL_SENSOR_REG_RO},
	{PL_CFG, "PL_CFG", .reset = 0x80},
	{PL_COUNT, "PL_COUNT"},
	{PL_BF_ZCOMP, "PL_BF_ZCOMP", .reset = 0x84},
	{PL_THS_REG, "PL_THS_REG", .reset = 0x44},
	{A_FFMT_CFG, "A_FFMT_CFG"},
	{A_FFMT_SRC, "A_FFMT_SRC", EMUL_SENSOR_REG_RO},
	{A_FFMT_THS, "A_FFMT_THS"},
	{A_FFMT_COUNT, "A_FFMT_COUNT"},
	{0x19, "Reserved"},
	{0x1A, "Reserved"},
	{0x1B, "Reserved"},
	{0x1C, "Reserved"},
	{TRANSIENT_CFG, "TRANSIENT_CFG"},
	{TRANSIENT_SRC, "TRANSIENT_SRC", EMUL_SENSOR_REG_RO},
	{TRANSIENT_THS, "TRANSIENT_THS"},
	{TRANSIENT_COUNT, "TRANSIENT_COUNT"},
	{PULSE_CFG, "PULSE_CFG"},
	{PULSE_SRC, "PULSE_SRC", EMUL_SENSOR_REG_RO},
	{PULSE_THSX, "PULSE_THSX"},
	{PULSE_THSY, "PULSE_THSY"},
	{PULSE_THSZ, "PULSE_THSZ"},
	{PULSE_TMLT, "PULSE_TMLT"},
	{PULSE_LTCY, "PULSE_LTCY"},
	{PULSE_WIND, "PULSE_WIND"},
	{ASLP_COUNT, "ASLP_COUNT"},
	/* aslp_rate[7:6], dr[5:3], lnoise[2], f_read[1], active[0] (Table 32). Setting
	 * active while the device was in standby starts continuous conversion (rule b).
	 */
	{CTRL_REG1, "CTRL_REG1", .convert_on_write = BIT(0), .requires_standby = true},
	/* st[7], rst[6], reserved[5], smods[4:3], slpe[2], mods[1:0] (Table 36). rst
	 * self-clears and restores every register to its reset value (Section 14.1.9).
	 */
	{CTRL_REG2, "CTRL_REG2", .write_mask = 0xDF, .self_clear = BIT(6),
	 .reset_on_write = BIT(6)},
	{CTRL_REG3, "CTRL_REG3"},
	{CTRL_REG4, "CTRL_REG4"},
	{CTRL_REG5, "CTRL_REG5"},
	{OFF_X, "OFF_X"},
	{OFF_Y, "OFF_Y"},
	{OFF_Z, "OFF_Z"},
	{M_DR_STATUS, "M_DR_STATUS", EMUL_SENSOR_REG_RO},
	{M_OUT_X_MSB, "M_OUT_X_MSB", EMUL_SENSOR_REG_RO, .read_clears = {M_DR_STATUS, DR_XDR}},
	{M_OUT_X_LSB, "M_OUT_X_LSB", EMUL_SENSOR_REG_RO},
	{M_OUT_Y_MSB, "M_OUT_Y_MSB", EMUL_SENSOR_REG_RO, .read_clears = {M_DR_STATUS, DR_YDR}},
	{M_OUT_Y_LSB, "M_OUT_Y_LSB", EMUL_SENSOR_REG_RO},
	{M_OUT_Z_MSB, "M_OUT_Z_MSB", EMUL_SENSOR_REG_RO,
	 .read_clears = {M_DR_STATUS, DR_ZDR | DR_ZYXDR}},
	{M_OUT_Z_LSB, "M_OUT_Z_LSB", EMUL_SENSOR_REG_RO},
	/* Hybrid-mode time-synchronized copy of the acceleration data, not a distinct
	 * measurement; see the comment above the channel table.
	 */
	{CMP_X_MSB, "CMP_X_MSB", EMUL_SENSOR_REG_RO},
	{CMP_X_LSB, "CMP_X_LSB", EMUL_SENSOR_REG_RO},
	{CMP_Y_MSB, "CMP_Y_MSB", EMUL_SENSOR_REG_RO},
	{CMP_Y_LSB, "CMP_Y_LSB", EMUL_SENSOR_REG_RO},
	{CMP_Z_MSB, "CMP_Z_MSB", EMUL_SENSOR_REG_RO},
	{CMP_Z_LSB, "CMP_Z_LSB", EMUL_SENSOR_REG_RO},
	{M_OFF_X_MSB, "M_OFF_X_MSB"},
	{M_OFF_X_LSB, "M_OFF_X_LSB"},
	{M_OFF_Y_MSB, "M_OFF_Y_MSB"},
	{M_OFF_Y_LSB, "M_OFF_Y_LSB"},
	{M_OFF_Z_MSB, "M_OFF_Z_MSB"},
	{M_OFF_Z_LSB, "M_OFF_Z_LSB"},
	{MAX_X_MSB, "MAX_X_MSB", EMUL_SENSOR_REG_RO},
	{MAX_X_LSB, "MAX_X_LSB", EMUL_SENSOR_REG_RO},
	{MAX_Y_MSB, "MAX_Y_MSB", EMUL_SENSOR_REG_RO},
	{MAX_Y_LSB, "MAX_Y_LSB", EMUL_SENSOR_REG_RO},
	{MAX_Z_MSB, "MAX_Z_MSB", EMUL_SENSOR_REG_RO},
	{MAX_Z_LSB, "MAX_Z_LSB", EMUL_SENSOR_REG_RO},
	{MIN_X_MSB, "MIN_X_MSB", EMUL_SENSOR_REG_RO},
	{MIN_X_LSB, "MIN_X_LSB", EMUL_SENSOR_REG_RO},
	{MIN_Y_MSB, "MIN_Y_MSB", EMUL_SENSOR_REG_RO},
	{MIN_Y_LSB, "MIN_Y_LSB", EMUL_SENSOR_REG_RO},
	{MIN_Z_MSB, "MIN_Z_MSB", EMUL_SENSOR_REG_RO},
	{MIN_Z_LSB, "MIN_Z_LSB", EMUL_SENSOR_REG_RO},
	/* 8-bit 2's complement, valid only when M_CTRL_REG1[m_hms] > 0b00 (Section 14.3.1) */
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO},
	{M_THS_CFG, "M_THS_CFG"},
	{M_THS_SRC, "M_THS_SRC", EMUL_SENSOR_REG_RO},
	{M_THS_X_MSB, "M_THS_X_MSB"},
	{M_THS_X_LSB, "M_THS_X_LSB"},
	{M_THS_Y_MSB, "M_THS_Y_MSB"},
	{M_THS_Y_LSB, "M_THS_Y_LSB"},
	{M_THS_Z_MSB, "M_THS_Z_MSB"},
	{M_THS_Z_LSB, "M_THS_Z_LSB"},
	{M_THS_COUNT, "M_THS_COUNT"},
	/*
	 * m_acal[7], m_rst[6], m_ost[5], m_os[4:2], m_hms[1:0] (Table 197). m_rst
	 * (degauss pulse) and m_ost (one-shot magnetometer measurement while in
	 * standby, Section 14.17.1) both self-clear; m_ost is not wired to
	 * emul_sensor_regmap_convert() because the framework's convert-on-write
	 * fires for every channel and the accelerometer must stay unconverted
	 * during this magnetometer-only one-shot.
	 */
	{M_CTRL_REG1, "M_CTRL_REG1", .self_clear = BIT(6) | BIT(5)},
	/* reserved[7:6], hyb_autoinc_mode[5], m_maxmin_dis[4], m_maxmin_dis_ths[3],
	 * m_maxmin_rst[2], m_rst_cnt[1:0] (Table 199)
	 */
	{M_CTRL_REG2, "M_CTRL_REG2", .write_mask = 0x3F},
	{M_CTRL_REG3, "M_CTRL_REG3"},
	{M_INT_SRC, "M_INT_SRC", EMUL_SENSOR_REG_RO},
	{A_VECM_CFG, "A_VECM_CFG"},
	{A_VECM_THS_MSB, "A_VECM_THS_MSB"},
	{A_VECM_THS_LSB, "A_VECM_THS_LSB"},
	{A_VECM_CNT, "A_VECM_CNT"},
	{A_VECM_INITX_MSB, "A_VECM_INITX_MSB"},
	{A_VECM_INITX_LSB, "A_VECM_INITX_LSB"},
	{A_VECM_INITY_MSB, "A_VECM_INITY_MSB"},
	{A_VECM_INITY_LSB, "A_VECM_INITY_LSB"},
	{A_VECM_INITZ_MSB, "A_VECM_INITZ_MSB"},
	{A_VECM_INITZ_LSB, "A_VECM_INITZ_LSB"},
	{M_VECM_CFG, "M_VECM_CFG"},
	{M_VECM_THS_MSB, "M_VECM_THS_MSB"},
	{M_VECM_THS_LSB, "M_VECM_THS_LSB"},
	{M_VECM_CNT, "M_VECM_CNT"},
	{M_VECM_INITX_MSB, "M_VECM_INITX_MSB"},
	{M_VECM_INITX_LSB, "M_VECM_INITX_LSB"},
	{M_VECM_INITY_MSB, "M_VECM_INITY_MSB"},
	{M_VECM_INITY_LSB, "M_VECM_INITY_LSB"},
	{M_VECM_INITZ_MSB, "M_VECM_INITZ_MSB"},
	{M_VECM_INITZ_LSB, "M_VECM_INITZ_LSB"},
	{A_FFMT_THS_X_MSB, "A_FFMT_THS_X_MSB"},
	{A_FFMT_THS_X_LSB, "A_FFMT_THS_X_LSB"},
	{A_FFMT_THS_Y_MSB, "A_FFMT_THS_Y_MSB"},
	{A_FFMT_THS_Y_LSB, "A_FFMT_THS_Y_LSB"},
	{A_FFMT_THS_Z_MSB, "A_FFMT_THS_Z_MSB"},
	{A_FFMT_THS_Z_LSB, "A_FFMT_THS_Z_LSB"},
	{0x79, "Reserved"},
};

/*
 * Acceleration: 14-bit left-justified two's complement in the MSB:LSB pair, bits [7:2] of
 * the LSB register (Section 14.4.1). XYZ_DATA_CFG[fs] selects sensitivity: 0.244 mg/LSB at
 * +/-2 g (4096 LSB/g), 0.488 mg/LSB at +/-4 g (2048 LSB/g), 0.976 mg/LSB at +/-8 g
 * (1024 LSB/g) (Table 61). fs = 0b11 is reserved and not modeled. Full range derives from
 * the 14-bit encoding. xdr/ydr/zdr in STATUS[DR_STATUS] signal a fresh sample (Table 20);
 * zyxdr is asserted alongside every axis and cleared with zdr on the read of OUT_Z_MSB, the
 * last register of the X,Y,Z burst (Table 21). Disabled while the magnetometer-only mode is
 * selected (M_CTRL_REG1[m_hms] = 0b01); the device.disabled condition below additionally
 * covers standby (CTRL_REG1[active] = 0).
 */
#define ACCEL(_chan, _reg, _ready)                                                             \
	{                                                                                        \
		.chan = _chan, .reg = _reg, .is_signed = true, .bits = 14, .pos = 2,            \
		.select = {XYZ_DATA_CFG, GENMASK(1, 0)},                                        \
		.variants = {{.lsb = G / 4096}, {.lsb = G / 2048}, {.lsb = G / 1024}},          \
		.ready = {STATUS, _ready | DR_ZYXDR}, .overrun = (_ready << 4),                 \
		.disabled = {M_CTRL_REG1, GENMASK(1, 0), 0x01},                                 \
	}

/*
 * Magnetometer: 16-bit two's complement, fixed sensitivity 0.1 uT/LSB = 0.001 G/LSB, full
 * range +/-1200 uT = +/-12 G (Section 8.1, feature list). xdr/ydr/zdr/xyzdr in M_DR_STATUS
 * behave like the accelerometer's DR_STATUS (Table 151/152). Disabled while
 * accelerometer-only mode is selected (M_CTRL_REG1[m_hms] = 0b00).
 */
#define MAGN(_chan, _reg, _ready)                                                              \
	{                                                                                        \
		.chan = _chan, .reg = _reg, .is_signed = true, .bits = 16, .lsb = 0.001,        \
		.min = -12.0, .max = 12.0,                                                      \
		.ready = {M_DR_STATUS, _ready | DR_ZYXDR}, .overrun = (_ready << 4),            \
		.disabled = {M_CTRL_REG1, GENMASK(1, 0), 0x00},                                 \
	}

static const struct emul_sensor_channel channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, OUT_X_MSB, DR_XDR),
	ACCEL(SENSOR_CHAN_ACCEL_Y, OUT_Y_MSB, DR_YDR),
	ACCEL(SENSOR_CHAN_ACCEL_Z, OUT_Z_MSB, DR_ZDR),
	MAGN(SENSOR_CHAN_MAGN_X, M_OUT_X_MSB, DR_XDR),
	MAGN(SENSOR_CHAN_MAGN_Y, M_OUT_Y_MSB, DR_YDR),
	MAGN(SENSOR_CHAN_MAGN_Z, M_OUT_Z_MSB, DR_ZDR),
	/*
	 * 8-bit 2's complement, 0.96 degC/LSB, uncalibrated (Section 14.3.1). Valid only
	 * when the magnetometer is active (M_CTRL_REG1[m_hms] != 0b00); no dedicated
	 * data-ready flag is documented for this channel.
	 */
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TEMP, .is_signed = true, .bits = 8, .lsb = 0.96,
	 .min = -40.0, .max = 125.0,
	 .disabled = {M_CTRL_REG1, GENMASK(1, 0), 0x00}},
};

/*
 * Not modeled, per the NOT MODELABLE rules or the model boundaries in emulators.rst:
 * - FIFO (F_SETUP, TRIG_CFG, F_STATUS alias of STATUS): buffering/timing, out of scope.
 * - Freefall/motion, transient, pulse/tap, landscape/portrait, vector-magnitude and
 *   magnetic-threshold event detectors: derived/piecewise functions of the acceleration or
 *   magnetic samples, not distinct measurable channels.
 * - MAX_X/Y/Z, MIN_X/Y/Z and the hard-iron auto-calibration (M_CTRL_REG1[m_acal]): internal
 *   calibration aids, not a measurement.
 * - CMP_X/Y/Z: hybrid-mode time-synchronized copy of the accelerometer data, already
 *   represented by SENSOR_CHAN_ACCEL_X/Y/Z; a single emul_sensor_channel entry cannot alias
 *   two source registers, so only the primary OUT_x registers are modeled.
 * - Accelerometer self-test (CTRL_REG2[st]) and reduced-noise mode (CTRL_REG1[lnoise]):
 *   analog output-level changes, not a register encoding.
 * - Fast-read mode (CTRL_REG1[f_read]): changes the register layout (drops the LSB bytes
 *   from the burst); not representable by a single fixed channel encoding.
 */

EMUL_SENSOR_REGMAP_DEFINE(regs, channels, .big_endian = true,
	.disabled = {CTRL_REG1, BIT(0), 0x00});
