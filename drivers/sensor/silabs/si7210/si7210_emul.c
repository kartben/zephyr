/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT silabs_si7210

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Si7210 I2C Hall Effect Magnetic Position and Temperature Sensor Data Sheet, Rev. 1.4,
 * section 4 Register Definitions: 21 registers in 0xc0 - 0xe4, named after the fields the
 * register table lists for each address.
 * https://www.silabs.com/documents/public/data-sheets/si7210-datasheet.pdf
 */
enum {
	CHIPID = 0xc0,
	DSPSIGM = 0xc1,
	DSPSIGL = 0xc2,
	DSPSIGSEL = 0xc3,
	MEAS = 0xc4,
	ARAUTOINC = 0xc5,
	SW_LOW4FIELD = 0xc6,
	SW_FIELDPOLSEL = 0xc7,
	SLTIME = 0xc8,
	SW_TAMPER = 0xc9,
	A0 = 0xca,
	A1 = 0xcb,
	A2 = 0xcc,
	DF_BURSTSIZE = 0xcd,
	A3 = 0xce,
	A4 = 0xcf,
	A5 = 0xd0,
	OTP_ADDR = 0xe1,
	OTP_DATA = 0xe2,
	OTP_READ_EN = 0xe3,
	TM_FG = 0xe4,
};

static const struct emul_sensor_reg registers[] = {
	/* chipid 0x1 in bits 7:4, revid 0x4 (revision B) in bits 3:0 */
	{CHIPID, "chipid/revid", EMUL_SENSOR_REG_RO, .reset = 0x14},
	/* Bit 7 is the fresh flag, cleared once Dspsigm has been read */
	{DSPSIGM, "Dspsigm", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(7)},
	{DSPSIGL, "Dspsigl", EMUL_SENSOR_REG_RO},
	{DSPSIGSEL, "dspsigsel", .write_mask = 0x07},
	/* meas is read only, oneburst auto clears once the conversion initiates */
	{MEAS, "meas/Usestore/oneburst/stop/sleep", .write_mask = 0x0f,
	 .self_clear = BIT(2), .convert_on_write = BIT(2)},
	{ARAUTOINC, "arautoinc", .write_mask = 0x01},
	/* 0xc6 - 0xd0 hold trim and switch settings loaded from OTP, part number specific */
	{SW_LOW4FIELD, "sw_low4field/sw_op"},
	{SW_FIELDPOLSEL, "sw_fieldpolsel/sw_hyst"},
	{SLTIME, "slTime"},
	/* slTimeena is normally factory set to 1 (section 3) */
	{SW_TAMPER, "sw_tamper/slFast/slTimeena", .reset = 0x01},
	{A0, "a0"},
	{A1, "a1"},
	{A2, "a2"},
	{DF_BURSTSIZE, "df_burstsize/df_bw/df_iir"},
	{A3, "a3"},
	{A4, "a4"},
	{A5, "a5"},
	{OTP_ADDR, "otp_addr"},
	{OTP_DATA, "otp_data", EMUL_SENSOR_REG_RO},
	/* otp_busy is read only, otp_read_en auto clears */
	{OTP_READ_EN, "otp_read_en/otp_busy", .write_mask = 0x02, .self_clear = BIT(1)},
	{TM_FG, "tm_fg", .write_mask = 0x03},
};

static const struct emul_sensor_channel channels[] = {
	/*
	 * 15b unsigned code split over Dspsigm[6:0] and Dspsigl, 16384 is zero field
	 * (section 4.1.2). 0.00125 mT per LSB on the 20 mT scale, which section 2 states as
	 * 1 G = 0.1 mT, so 0.0125 G per LSB and -204.8 G at code 0, over a +/-20.47 mT range.
	 * The scale comes from the a0 - a5 OTP gain coefficients, not from a register field.
	 */
	{.chan = SENSOR_CHAN_MAGN_Z, .reg = DSPSIGM, .bits = 15, .lsb = 0.0125,
	 .offset = -204.8, .min = -204.7, .max = 204.7,
	 .ready = {.reg = DSPSIGM, .mask = BIT(7)},
	 .disabled = {.reg = DSPSIGSEL, .mask = 0x07, .value = 0x01}},
};

static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	/* oneburst leaves the stop bit set once the conversion completes (section 4.1.2) */
	if (addr == MEAS && (data->regs[addr] & BIT(2)) != 0U) {
		data->regs[addr] = (data->regs[addr] & ~BIT(2)) | BIT(1);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.increment = {ARAUTOINC, BIT(0)},
	.disabled = {.reg = MEAS, .mask = BIT(1), .value = BIT(1)},
	.write = write);
