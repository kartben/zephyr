/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT memsic_mmc56x3

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * MEMSIC MMC5603NJ Rev. B, Formal release 1/17/2022, "REGISTER MAP" and "REGISTER DETAILS"
 * (pages 6-11):
 * https://www.memsic.com/Public/Uploads/uploadfile/files/20220119/MMC5603NJDatasheetRev.B.pdf
 * The MMC5633NJL shares this register map (same die, different package).
 */
enum {
	XOUT0 = 0x00,
	XOUT1 = 0x01,
	YOUT0 = 0x02,
	YOUT1 = 0x03,
	ZOUT0 = 0x04,
	ZOUT1 = 0x05,
	XOUT2 = 0x06,
	YOUT2 = 0x07,
	ZOUT2 = 0x08,
	TOUT = 0x09,
	STATUS1 = 0x18,
	ODR = 0x1a,
	INTERNAL_CONTROL_0 = 0x1b,
	INTERNAL_CONTROL_1 = 0x1c,
	INTERNAL_CONTROL_2 = 0x1d,
	ST_X_TH = 0x1e,
	ST_Y_TH = 0x1f,
	ST_Z_TH = 0x20,
	ST_X = 0x27,
	ST_Y = 0x28,
	ST_Z = 0x29,
	PRODUCT_ID = 0x39,
};

/* Meas_m_done (bit 6 of Status1) clears when any magnetic data register (Xout/Yout/Zout) is
 * read; Meas_t_done (bit 7) clears when Tout is read.
 */
#define MAGN_READ_CLEARS .read_clears = {.reg = STATUS1, .mask = BIT(6)}

static const struct emul_sensor_reg registers[] = {
	{XOUT0, "Xout0", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{XOUT1, "Xout1", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{YOUT0, "Yout0", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{YOUT1, "Yout1", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{ZOUT0, "Zout0", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{ZOUT1, "Zout1", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	/* Bits 7:4 hold Xout[3:0]/Yout[3:0]/Zout[3:0] for 18/20-bit operation, bits 3:0 read 0 */
	{XOUT2, "Xout2", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{YOUT2, "Yout2", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	{ZOUT2, "Zout2", EMUL_SENSOR_REG_RO, MAGN_READ_CLEARS},
	/* Unsigned, 0.8 degC/LSB typ, 00H is -75 degC */
	{TOUT, "Tout", EMUL_SENSOR_REG_RO, .read_clears = {.reg = STATUS1, .mask = BIT(7)}},
	/* Meas_t_done, Meas_m_done, Sat_sensor, OTP_read_done, ST_Fail, Mdt_flag_int,
	 * Meas_t_done_int, Meas_m_done_int; all factory/status bits, no reset value given
	 */
	{STATUS1, "Status1", EMUL_SENSOR_REG_RO},
	/* Continuous-mode measurement frequency; 0 keeps the device out of continuous mode */
	{ODR, "ODR", .write_mask = 0xff},
	/* Cmm_freq_en, Auto_st_en, Auto_SR_en, Do_Reset, Do_Set, Start_MDT, Take_meas_T,
	 * Take_meas_M. Take_meas_M/T trigger a conversion and self-clear; Do_Set/Do_Reset and
	 * Auto_st_en/Cmm_freq_en self-clear once their (unmodeled) operation completes.
	 */
	{INTERNAL_CONTROL_0, "Internal control 0", .write_mask = 0xff,
	 .self_clear = BIT(7) | BIT(6) | BIT(4) | BIT(3) | BIT(1) | BIT(0),
	 .convert_on_write = BIT(1) | BIT(0)},
	/* Sw_reset, St_enm, St_enp, Z-inhibit, Y-inhibit, X-inhibit, BW1, BW0. Sw_reset restores
	 * power-up state of every register and re-reads OTP.
	 */
	{INTERNAL_CONTROL_1, "Internal control 1", .write_mask = 0xff,
	 .reset_on_write = BIT(7)},
	/* hpower, INT_meas_done_en, INT_mdt_en, Cmm_en, En_prd_set, Prd_set[2:0] */
	{INTERNAL_CONTROL_2, "Internal control 2", .write_mask = 0xff},
	{ST_X_TH, "ST_X_TH", .write_mask = 0xff},
	{ST_Y_TH, "ST_Y_TH", .write_mask = 0xff},
	{ST_Z_TH, "ST_Z_TH", .write_mask = 0xff},
	/* Factory-stored selftest set value; reset value is not specified by the datasheet */
	{ST_X, "ST_X", .write_mask = 0xff},
	{ST_Y, "ST_Y", .write_mask = 0xff},
	{ST_Z, "ST_Z", .write_mask = 0xff},
	{PRODUCT_ID, "Product ID", EMUL_SENSOR_REG_RO, .reset = 0x10},
};

/*
 * Each axis is a 20-bit unsigned value split as reg0[19:12], reg1[11:4], reg2[7:4] = [3:0]
 * (reg2 is not adjacent to reg0/reg1, so the model uses only the top 16 bits, matching the
 * datasheet's own "16bits operation mode" data definition: Xout[19:4] from Xout0/Xout1). The
 * 18/20-bit extension held in Xout2/Yout2/Zout2 is not modeled; see unsupported.
 *
 * Sensitivity is 1024 counts/G at 16-bit resolution (Table, Sensitivity row, "With16bits
 * operation"), with the null (zero) field output at half scale, code 32768 (Null Field Output
 * table, "With16bits operation"). Field range is +-30 G (Field Range spec).
 *
 * X/Y/Z-inhibit (Internal control 1) skip that axis during a measurement and retain its last
 * output value, modeled as the channel's disabled condition.
 */
#define MAGN(_chan, _reg, _inhibit_bit)                                                         \
	{                                                                                        \
		.chan = _chan, .reg = _reg, .bits = 16, .lsb = 1.0 / 1024, .offset = -32.0,     \
		.min = -30.0, .max = 30.0, .ready = {.reg = STATUS1, .mask = BIT(6)},          \
		.disabled = {.reg = INTERNAL_CONTROL_1, .mask = BIT(_inhibit_bit),             \
			     .value = BIT(_inhibit_bit)},                                       \
	}

static const struct emul_sensor_channel channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, XOUT0, 2),
	MAGN(SENSOR_CHAN_MAGN_Y, YOUT0, 3),
	MAGN(SENSOR_CHAN_MAGN_Z, ZOUT0, 4),
	/* Unsigned 8-bit, 0.8 degC/LSB, 00H = -75 degC (Temperature Out register description) */
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TOUT, .bits = 8, .lsb = 0.8, .offset = -75.0,
	 .min = -75.0, .max = 125.0, .ready = {.reg = STATUS1, .mask = BIT(7)}},
};

/* Continuous mode (Cmm_en) is not a self-clearing command: unlike Take_meas_M/T it has no
 * convert_on_write bit of its own. Enabling it (0 -> 1) starts periodic conversions that would
 * otherwise not appear until the next ODR tick, so the first sample is produced immediately.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == INTERNAL_CONTROL_2 && (old & BIT(4)) == 0U &&
	    (data->regs[INTERNAL_CONTROL_2] & BIT(4)) != 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .write = write);
