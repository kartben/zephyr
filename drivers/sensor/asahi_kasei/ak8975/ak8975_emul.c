/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT asahi_kasei_ak8975

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * AKM AK8975/AK8975C datasheet MS1187-E-02, 2010/05, section 8 (Registers), register map
 * table 8.2: https://www.robotpark.com/image/data/PRO/91462/AK8975.pdf
 */
enum {
	WIA = 0x00,
	INFO = 0x01,
	ST1 = 0x02,
	HXL = 0x03,
	HXH = 0x04,
	HYL = 0x05,
	HYH = 0x06,
	HZL = 0x07,
	HZH = 0x08,
	ST2 = 0x09,
	CNTL = 0x0a,
	RSV = 0x0b,
	ASTC = 0x0c,
	TS1 = 0x0d,
	TS2 = 0x0e,
	I2CDIS = 0x0f,
	ASAX = 0x10,
	ASAY = 0x11,
	ASAZ = 0x12,
};

static const struct emul_sensor_reg registers[] = {
	{WIA, "WIA", EMUL_SENSOR_REG_RO, .reset = 0x48},
	/* AKM device information; datasheet does not publish a fixed reset value */
	{INFO, "INFO", EMUL_SENSOR_REG_RO},
	{ST1, "ST1", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(0)},
	{HXL, "HXL", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	{HXH, "HXH", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	{HYL, "HYL", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	{HYH, "HYH", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	{HZL, "HZL", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	{HZH, "HZH", EMUL_SENSOR_REG_RO, .read_clears = {ST1, BIT(0)}},
	/* DERR (bit 2) clears on read; HOFL (bit 3) only clears when a new measurement starts */
	{ST2, "ST2", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(2), .read_clears = {ST1, BIT(0)}},
	{CNTL, "CNTL", .write_mask = 0x0f},
	{RSV, "RSV", .write_mask = 0xff},
	/* Only D6 (SELF) is defined; the datasheet says other bits must stay 0 */
	{ASTC, "ASTC", .write_mask = BIT(6)},
	{TS1, "TS1", .write_mask = 0xff},
	{TS2, "TS2", .write_mask = 0xff},
	{I2CDIS, "I2CDIS", .write_mask = BIT(0)},
	/*
	 * Fuse ROM sensitivity trim, non-volatile and unaffected by POR. ASA=128 is the
	 * neutral value for which the host-side adjustment formula (section 8.3.11) is an
	 * identity; the datasheet does not specify a numeric default for this per-part value.
	 */
	{ASAX, "ASAX", EMUL_SENSOR_REG_RO, .reset = 0x80},
	{ASAY, "ASAY", EMUL_SENSOR_REG_RO, .reset = 0x80},
	{ASAZ, "ASAZ", EMUL_SENSOR_REG_RO, .reset = 0x80},
};

/*
 * HXL/HXH, HYL/HYH and HZL/HZH each hold a two's complement value in Little Endian byte
 * order (section 8.3.4). The ADC itself is 13 bit but the pair is written as a full 16-bit
 * two's complement word sign-extended from that range, decimal -4096 to +4095 (table 8.3),
 * so the register field spans all 16 bits while min/max bound it to the 13-bit range.
 * Sensitivity BSE is 0.3 uT/LSB typical after fuse trim (section 5.3.3), i.e. 0.003 G/LSB.
 */
#define MAGN_LSB_GAUSS 0.003
#define MAGN(_chan, _reg) \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .lsb = MAGN_LSB_GAUSS, \
	 .min = -4096 * MAGN_LSB_GAUSS, .max = 4095 * MAGN_LSB_GAUSS, \
	 .ready = {ST1, BIT(0)}}

static const struct emul_sensor_channel channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, HXL),
	MAGN(SENSOR_CHAN_MAGN_Y, HYL),
	MAGN(SENSOR_CHAN_MAGN_Z, HZL),
};

/*
 * Any write to CNTL reinitializes ST1 through ST2 (section 8.3.6), regardless of the mode
 * written. Single measurement ("0001") and self-test ("1000") then convert the retained
 * inputs and return the device to Power-down mode ("0000") on their own; the datasheet
 * requires Power-down mode ("0000") to have been set immediately before either (section
 * 6.3), so a mode write is honored only when the previous CNTL value was 0x00. Fuse ROM
 * access mode ("1111") does not convert and stays set until the host writes "0000" itself.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	static const uint8_t status_regs[] = {ST1, HXL, HXH, HYL, HYH, HZL, HZH, ST2};
	uint32_t mode;

	if (reg != CNTL) {
		return;
	}

	for (size_t i = 0; i < ARRAY_SIZE(status_regs); i++) {
		data->regs[status_regs[i]] = 0;
	}

	mode = data->regs[CNTL] & GENMASK(3, 0);
	if ((mode == 0x1 || mode == 0x8) && old == 0x00) {
		emul_sensor_regmap_convert(target);
		data->regs[CNTL] = 0x00;
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .byte_addressed = true, .write = write);
