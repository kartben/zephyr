/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT microchip_pac194x

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * DS20006543C, sections 6.5 (auto-incrementing pointer) and 7.0 (register set):
 * https://ww1.microchip.com/downloads/aemDocuments/documents/MSLD/ProductDocuments/DataSheets/
 * PAC194X-Family-Data-Sheet-DS20006543.pdf
 *
 * Reset values and the CHANNEL_N_OFF default in CTRL are those of the PAC1944-1 (4-channel,
 * high-side); PAC1941/42/43 differ only in CTRL's POR value and the Product ID (Register 7-2,
 * 7-37). 0x2A-0x2F and 0x4C-0xFC are excluded from the auto-incrementing loops (Figure 6-2) and
 * are not valid addresses.
 */
enum {
	REFRESH = 0x00,
	CTRL = 0x01,
	ACC_COUNT = 0x02,
	VACC1 = 0x03,
	VACC2 = 0x04,
	VACC3 = 0x05,
	VACC4 = 0x06,
	VBUS1 = 0x07,
	VBUS2 = 0x08,
	VBUS3 = 0x09,
	VBUS4 = 0x0a,
	VSENSE1 = 0x0b,
	VSENSE2 = 0x0c,
	VSENSE3 = 0x0d,
	VSENSE4 = 0x0e,
	VBUS1_AVG = 0x0f,
	VBUS2_AVG = 0x10,
	VBUS3_AVG = 0x11,
	VBUS4_AVG = 0x12,
	VSENSE1_AVG = 0x13,
	VSENSE2_AVG = 0x14,
	VSENSE3_AVG = 0x15,
	VSENSE4_AVG = 0x16,
	VPOWER1 = 0x17,
	VPOWER2 = 0x18,
	VPOWER3 = 0x19,
	VPOWER4 = 0x1a,
	SMBUS_SETTINGS = 0x1c,
	NEG_PWR_FSR = 0x1d,
	REFRESH_G = 0x1e,
	REFRESH_V = 0x1f,
	SLOW = 0x20,
	CTRL_ACT = 0x21,
	NEG_PWR_FSR_ACT = 0x22,
	CTRL_LAT = 0x23,
	NEG_PWR_FSR_LAT = 0x24,
	ACCUM_CONFIG = 0x25,
	ALERT_STATUS = 0x26,
	SLOW_ALERT1 = 0x27,
	GPIO_ALERT2 = 0x28,
	ACC_FULLNESS_LIMITS = 0x29,
	OC_LIMIT1 = 0x30,
	OC_LIMIT2 = 0x31,
	OC_LIMIT3 = 0x32,
	OC_LIMIT4 = 0x33,
	UC_LIMIT1 = 0x34,
	UC_LIMIT2 = 0x35,
	UC_LIMIT3 = 0x36,
	UC_LIMIT4 = 0x37,
	OP_LIMIT1 = 0x38,
	OP_LIMIT2 = 0x39,
	OP_LIMIT3 = 0x3a,
	OP_LIMIT4 = 0x3b,
	OV_LIMIT1 = 0x3c,
	OV_LIMIT2 = 0x3d,
	OV_LIMIT3 = 0x3e,
	OV_LIMIT4 = 0x3f,
	UV_LIMIT1 = 0x40,
	UV_LIMIT2 = 0x41,
	UV_LIMIT3 = 0x42,
	UV_LIMIT4 = 0x43,
	OC_LIMIT_NSAMPLES = 0x44,
	UC_LIMIT_NSAMPLES = 0x45,
	OP_LIMIT_NSAMPLES = 0x46,
	OV_LIMIT_NSAMPLES = 0x47,
	UV_LIMIT_NSAMPLES = 0x48,
	ALERT_ENABLE = 0x49,
	ACCUM_CONFIG_ACT = 0x4a,
	ACCUM_CONFIG_LAT = 0x4b,
	PRODUCT_ID = 0xfd,
	MANUFACTURER_ID = 0xfe,
	REVISION_ID = 0xff,
};

/* CHANNEL_N_OFF[3:0] in CTRL bits 7:4; bit 7 is Channel 1, bit 4 is Channel 4 (Register 7-2). */
#define CHANNEL_OFF(n) BIT(8 - (n))

static const struct emul_sensor_reg registers[] = {
	{REFRESH, "REFRESH"},
	/* SAMPLE_MODE=0000 (1024 SPS), GPIO_ALERT2=01, SLOW_ALERT1=11, all channels on (7-2) */
	{CTRL, "CTRL", .bytes = 2, .reset = 0x0700, .write_mask = 0xfff0},
	{ACC_COUNT, "ACC_COUNT", EMUL_SENSOR_REG_RO, .bytes = 4},
	{VACC1, "VACC1", EMUL_SENSOR_REG_RO, .bytes = 7},
	{VACC2, "VACC2", EMUL_SENSOR_REG_RO, .bytes = 7},
	{VACC3, "VACC3", EMUL_SENSOR_REG_RO, .bytes = 7},
	{VACC4, "VACC4", EMUL_SENSOR_REG_RO, .bytes = 7},
	{VBUS1, "VBUS1", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS2, "VBUS2", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS3, "VBUS3", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS4, "VBUS4", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE1, "VSENSE1", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE2, "VSENSE2", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE3, "VSENSE3", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE4, "VSENSE4", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS1_AVG, "VBUS1_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS2_AVG, "VBUS2_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS3_AVG, "VBUS3_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VBUS4_AVG, "VBUS4_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE1_AVG, "VSENSE1_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE2_AVG, "VSENSE2_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE3_AVG, "VSENSE3_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VSENSE4_AVG, "VSENSE4_AVG", EMUL_SENSOR_REG_RO, .bytes = 2},
	{VPOWER1, "VPOWER1", EMUL_SENSOR_REG_RO, .bytes = 4},
	{VPOWER2, "VPOWER2", EMUL_SENSOR_REG_RO, .bytes = 4},
	{VPOWER3, "VPOWER3", EMUL_SENSOR_REG_RO, .bytes = 4},
	{VPOWER4, "VPOWER4", EMUL_SENSOR_REG_RO, .bytes = 4},
	{SMBUS_SETTINGS, "SMBUS_SETTINGS", .reset = 0x10, .write_mask = 0xff},
	{NEG_PWR_FSR, "NEG_PWR_FSR", .bytes = 2, .write_mask = 0xffff},
	{REFRESH_G, "REFRESH_G"},
	{REFRESH_V, "REFRESH_V"},
	/* SLOW/SLOW_LH/SLOW_HL (bits 7:5) track the SLOW pin and its edges; not host-writable */
	{SLOW, "SLOW", .write_mask = 0x1e},
	{CTRL_ACT, "CTRL_ACT", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x0700},
	{NEG_PWR_FSR_ACT, "NEG_PWR_FSR_ACT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{CTRL_LAT, "CTRL_LAT", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x0700},
	{NEG_PWR_FSR_LAT, "NEG_PWR_FSR_LAT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{ACCUM_CONFIG, "ACCUM_CONFIG", .write_mask = 0xff},
	{ALERT_STATUS, "ALERT_STATUS", EMUL_SENSOR_REG_RO, .bytes = 3, .clear_on_read = 0xfffffc},
	{SLOW_ALERT1, "SLOW_ALERT1", .bytes = 3, .write_mask = 0xfffffe},
	{GPIO_ALERT2, "GPIO_ALERT2", .bytes = 3, .write_mask = 0xfffffe},
	/* CHx ACC FULL=01 (15/16), ACC_COUNT FULL=01 (15/16) (Register 7-23) */
	{ACC_FULLNESS_LIMITS, "ACC_FULLNESS_LIMITS", .bytes = 2, .reset = 0x5540,
	 .write_mask = 0xffc0},
	{OC_LIMIT1, "OC_LIMIT1", .bytes = 2, .write_mask = 0xffff},
	{OC_LIMIT2, "OC_LIMIT2", .bytes = 2, .write_mask = 0xffff},
	{OC_LIMIT3, "OC_LIMIT3", .bytes = 2, .write_mask = 0xffff},
	{OC_LIMIT4, "OC_LIMIT4", .bytes = 2, .write_mask = 0xffff},
	{UC_LIMIT1, "UC_LIMIT1", .bytes = 2, .write_mask = 0xffff},
	{UC_LIMIT2, "UC_LIMIT2", .bytes = 2, .write_mask = 0xffff},
	{UC_LIMIT3, "UC_LIMIT3", .bytes = 2, .write_mask = 0xffff},
	{UC_LIMIT4, "UC_LIMIT4", .bytes = 2, .write_mask = 0xffff},
	{OP_LIMIT1, "OP_LIMIT1", .bytes = 3, .write_mask = 0xffffff},
	{OP_LIMIT2, "OP_LIMIT2", .bytes = 3, .write_mask = 0xffffff},
	{OP_LIMIT3, "OP_LIMIT3", .bytes = 3, .write_mask = 0xffffff},
	{OP_LIMIT4, "OP_LIMIT4", .bytes = 3, .write_mask = 0xffffff},
	{OV_LIMIT1, "OV_LIMIT1", .bytes = 2, .write_mask = 0xffff},
	{OV_LIMIT2, "OV_LIMIT2", .bytes = 2, .write_mask = 0xffff},
	{OV_LIMIT3, "OV_LIMIT3", .bytes = 2, .write_mask = 0xffff},
	{OV_LIMIT4, "OV_LIMIT4", .bytes = 2, .write_mask = 0xffff},
	{UV_LIMIT1, "UV_LIMIT1", .bytes = 2, .write_mask = 0xffff},
	{UV_LIMIT2, "UV_LIMIT2", .bytes = 2, .write_mask = 0xffff},
	{UV_LIMIT3, "UV_LIMIT3", .bytes = 2, .write_mask = 0xffff},
	{UV_LIMIT4, "UV_LIMIT4", .bytes = 2, .write_mask = 0xffff},
	{OC_LIMIT_NSAMPLES, "OC_LIMIT_NSAMPLES", .write_mask = 0xff},
	{UC_LIMIT_NSAMPLES, "UC_LIMIT_NSAMPLES", .write_mask = 0xff},
	{OP_LIMIT_NSAMPLES, "OP_LIMIT_NSAMPLES", .write_mask = 0xff},
	{OV_LIMIT_NSAMPLES, "OV_LIMIT_NSAMPLES", .write_mask = 0xff},
	{UV_LIMIT_NSAMPLES, "UV_LIMIT_NSAMPLES", .write_mask = 0xff},
	{ALERT_ENABLE, "ALERT_ENABLE", .bytes = 3, .write_mask = 0xfffffe},
	{ACCUM_CONFIG_ACT, "ACCUM_CONFIG_ACT", EMUL_SENSOR_REG_RO},
	{ACCUM_CONFIG_LAT, "ACCUM_CONFIG_LAT", EMUL_SENSOR_REG_RO},
	/* PAC1944-1; PAC1941-1/42-1/43-1 read 68h/69h/6Ah, PAC1941-2/42-2 read 6Ch/6Dh */
	{PRODUCT_ID, "PRODUCT_ID", EMUL_SENSOR_REG_RO, .reset = 0x6b},
	{MANUFACTURER_ID, "MANUFACTURER_ID", EMUL_SENSOR_REG_RO, .reset = 0x54},
	{REVISION_ID, "REVISION_ID", EMUL_SENSOR_REG_RO, .reset = 0x02},
};

/*
 * VBUSn and VSENSEn are 16-bit unsigned in the default unipolar FSR (NEG_PWR_FSR CFG_VBn/VSn =
 * 00b): 0-9V and 0-100mV, LSB = FSR/65536 (DC table VBUS_LSB/VSENSE_LSB). The bipolar FSR
 * selections switch the same registers to two's complement and are not modeled: emul_sensor_
 * field carries no signedness, so a .select variant cannot flip emul_sensor_channel.is_signed.
 * Results are only meaningful after a REFRESH; see the write() callback below.
 */
#define CHANNEL_DISABLED(_off_bit)                                                                \
	{.reg = CTRL, .mask = CHANNEL_OFF(_off_bit), .value = CHANNEL_OFF(_off_bit)}

#define VBUS(_reg, _off_bit)                                                                      \
	{.chan = SENSOR_CHAN_VOLTAGE, .reg = (_reg), .bits = 16, .lsb = 9.0 / 65536, .min = 0,     \
	 .max = 9, .disabled = CHANNEL_DISABLED(_off_bit)}

#define VSENSE(_reg, _off_bit)                                                                    \
	{.chan = SENSOR_CHAN_VSHUNT, .reg = (_reg), .bits = 16, .lsb = 100.0 / 65536, .min = 0,    \
	 .max = 100, .disabled = CHANNEL_DISABLED(_off_bit)}

static const struct emul_sensor_channel channels[] = {
	VBUS(VBUS1, 1), VBUS(VBUS2, 2), VBUS(VBUS3, 3), VBUS(VBUS4, 4),
	VSENSE(VSENSE1, 1), VSENSE(VSENSE2, 2), VSENSE(VSENSE3, 3), VSENSE(VSENSE4, 4),
};

/*
 * VBUS, VSENSE and VPOWER results only update in the readable registers when a REFRESH,
 * REFRESH_G or REFRESH_V command is sent (Sections 5.2-5.4); a change to the sampling mode
 * (leaving Sleep, enabling a channel) takes effect this way too, not on the CTRL write itself.
 * REFRESH and REFRESH_G additionally latch the currently active configuration into the _ACT
 * registers, first saving the previous _ACT into _LAT (Registers 7-15 to 7-18, 7-35, 7-36), and
 * clear the SLOW pin transition flags (Section 5.7); REFRESH_V does neither.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);

	if (reg != REFRESH && reg != REFRESH_G && reg != REFRESH_V) {
		return;
	}

	if (reg != REFRESH_V) {
		data->regs[CTRL_LAT] = data->regs[CTRL_ACT];
		data->regs[NEG_PWR_FSR_LAT] = data->regs[NEG_PWR_FSR_ACT];
		data->regs[ACCUM_CONFIG_LAT] = data->regs[ACCUM_CONFIG_ACT];
		data->regs[CTRL_ACT] = data->regs[CTRL];
		data->regs[NEG_PWR_FSR_ACT] = data->regs[NEG_PWR_FSR];
		data->regs[ACCUM_CONFIG_ACT] = data->regs[ACCUM_CONFIG];
		data->regs[SLOW] &= ~(BIT(6) | BIT(5));
	}

	emul_sensor_regmap_convert(target);
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.disabled = {.reg = CTRL, .mask = GENMASK(15, 12), .value = GENMASK(15, 12)},
	.write = write);
