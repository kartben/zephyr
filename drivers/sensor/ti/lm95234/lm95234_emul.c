/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT national_lm95234

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SNIS136D - AUGUST 2006 - REVISED MARCH 2013, "LM95234 Registers", Table 8. Register Summary
 * (pages 21-22) plus the per-register bit tables that follow it (pages 21-33):
 * https://www.ti.com/lit/ds/symlink/lm95234.pdf
 *
 * The local temperature and each of the four remote diode temperatures are each reported twice:
 * once as a 2's complement word (+127.875 degC/-128 degC range) and once as an unsigned word
 * (0 degC/+255.875 degC range, remote diodes only). Both aliases of a channel hold the identical
 * raw ADC code; Table 2 and Table 3 (and Table 4/5) show the same hex pattern for the same
 * temperature under both interpretations, so the "unsigned" registers are not a separate
 * measurement, just a second read address for the same conversion result.
 */
enum {
	COMMON_STATUS = 0x02,
	CONFIGURATION = 0x03,
	CONVERSION_RATE = 0x04,
	CHANNEL_CONVERSION_ENABLE = 0x05,
	FILTER_SETTING = 0x06,
	STATUS1_DIODE_FAULT = 0x07,
	STATUS2_TCRIT1 = 0x08,
	STATUS3_TCRIT2 = 0x09,
	STATUS4_TCRIT3 = 0x0a,
	TCRIT1_MASK = 0x0c,
	TCRIT2_MASK = 0x0d,
	TCRIT3_MASK = 0x0e,
	ONE_SHOT = 0x0f,
	LOCAL_TEMP_MSB = 0x10,
	REMOTE1_TEMP_MSB = 0x11,
	REMOTE2_TEMP_MSB = 0x12,
	REMOTE3_TEMP_MSB = 0x13,
	REMOTE4_TEMP_MSB = 0x14,
	REMOTE1_TEMP_MSB_U = 0x19,
	REMOTE2_TEMP_MSB_U = 0x1a,
	REMOTE3_TEMP_MSB_U = 0x1b,
	REMOTE4_TEMP_MSB_U = 0x1c,
	LOCAL_TEMP_LSB = 0x20,
	REMOTE1_TEMP_LSB = 0x21,
	REMOTE2_TEMP_LSB = 0x22,
	REMOTE3_TEMP_LSB = 0x23,
	REMOTE4_TEMP_LSB = 0x24,
	REMOTE1_TEMP_LSB_U = 0x29,
	REMOTE2_TEMP_LSB_U = 0x2a,
	REMOTE3_TEMP_LSB_U = 0x2b,
	REMOTE4_TEMP_LSB_U = 0x2c,
	DIODE_MODEL_SELECT = 0x30,
	REMOTE1_OFFSET = 0x31,
	REMOTE2_OFFSET = 0x32,
	REMOTE3_OFFSET = 0x33,
	REMOTE4_OFFSET = 0x34,
	DIODE_MODEL_STATUS = 0x38,
	LOCAL_TCRIT_LIMIT = 0x40,
	REMOTE1_TCRIT1_LIMIT = 0x41,
	REMOTE2_TCRIT1_LIMIT = 0x42,
	REMOTE3_TCRIT_LIMIT = 0x43,
	REMOTE4_TCRIT_LIMIT = 0x44,
	REMOTE1_TCRIT23_LIMIT = 0x49,
	REMOTE2_TCRIT23_LIMIT = 0x4a,
	COMMON_TCRIT_HYSTERESIS = 0x5a,
	MANUFACTURER_ID = 0xfe,
	REVISION_ID = 0xff,
};

/* Channel input indices, matching the order of channels[] below. */
enum { CH_LOCAL = 0, CH_REMOTE1 = 1, CH_REMOTE2 = 2, CH_REMOTE3 = 3, CH_REMOTE4 = 4 };

static const struct emul_sensor_reg registers[] = {
	/* All temperature readings power up at 0 degC, until the first conversion completes. */
	{LOCAL_TEMP_MSB, "Local Temp MSB", EMUL_SENSOR_REG_RO},
	{REMOTE1_TEMP_MSB, "Remote Temp 1 MSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE2_TEMP_MSB, "Remote Temp 2 MSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE3_TEMP_MSB, "Remote Temp 3 MSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE4_TEMP_MSB, "Remote Temp 4 MSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE1_TEMP_MSB_U, "Remote Temp 1 MSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE2_TEMP_MSB_U, "Remote Temp 2 MSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE3_TEMP_MSB_U, "Remote Temp 3 MSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE4_TEMP_MSB_U, "Remote Temp 4 MSB - Unsigned", EMUL_SENSOR_REG_RO},
	{LOCAL_TEMP_LSB, "Local Temp LSB", EMUL_SENSOR_REG_RO},
	{REMOTE1_TEMP_LSB, "Remote Temp 1 LSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE2_TEMP_LSB, "Remote Temp 2 LSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE3_TEMP_LSB, "Remote Temp 3 LSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE4_TEMP_LSB, "Remote Temp 4 LSB - Signed", EMUL_SENSOR_REG_RO},
	{REMOTE1_TEMP_LSB_U, "Remote Temp 1 LSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE2_TEMP_LSB_U, "Remote Temp 2 LSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE3_TEMP_LSB_U, "Remote Temp 3 LSB - Unsigned", EMUL_SENSOR_REG_RO},
	{REMOTE4_TEMP_LSB_U, "Remote Temp 4 LSB - Unsigned", EMUL_SENSOR_REG_RO},

	/* R4TE:R1TE default 0010, i.e. Remote 1 defaults to the 65 nm Intel/TruTherm model. */
	{DIODE_MODEL_SELECT, "Diode Model Select", .reset = 0x02, .write_mask = 0x1e},
	/* 2's complement, 0.5 degC/LSB, +63.5 degC/-64 degC; directly added to the ADC reading. */
	{REMOTE1_OFFSET, "Remote 1 Offset", .write_mask = 0xff},
	{REMOTE2_OFFSET, "Remote 2 Offset", .write_mask = 0xff},
	{REMOTE3_OFFSET, "Remote 3 Offset", .write_mask = 0xff},
	{REMOTE4_OFFSET, "Remote 4 Offset", .write_mask = 0xff},

	/* STBY (bit 6); R4QE/R3QE fault-queue enables (bits 1:0). Reset: active, R4/R3 queued. */
	{CONFIGURATION, "Configuration", .reset = 0x03, .write_mask = 0x43},
	/* CR[1:0]: 00 continuous, 01 0.364s, 10 1s, 11 2.5s. */
	{CONVERSION_RATE, "Conversion Rate", .reset = 0x02, .write_mask = 0x03},
	/* R4CE:LCE, all five channels enabled at reset. */
	{CHANNEL_CONVERSION_ENABLE, "Channel Conversion Enable", .reset = 0x1f,
	 .write_mask = 0x1f},
	/* R2F[1:0]/R1F[1:0]: 00 off, 01 basic filter, 10 reserved, 11 enhanced filter. */
	{FILTER_SETTING, "Filter Setting", .reset = 0x0f, .write_mask = 0x0f},
	/* Value written is a don't-care and is never stored (section "1-Shot"). */
	{ONE_SHOT, "1-Shot", .self_clear = 0xff, .write_mask = 0xff, .convert_on_write = 0xff,
	 .requires_standby = true},

	/*
	 * Diode fault detection (Status 1) and the TCRIT1/2/3 comparator outputs (Status 2-4,
	 * Diode Model Status, the TCRIT masks, the Tcrit limits and the hysteresis register) are
	 * present in the map with their documented addresses, reset values and write masks, but
	 * the comparator logic itself is not evaluated: it depends on the analog diode-fault
	 * detector and on the TCRITn pins, neither of which corresponds to a measurement channel.
	 */
	{COMMON_STATUS, "Common Status Register", EMUL_SENSOR_REG_RO},
	{STATUS1_DIODE_FAULT, "Status 1 (Diode Fault)", EMUL_SENSOR_REG_RO},
	{STATUS2_TCRIT1, "Status 2 (TCRIT1)", EMUL_SENSOR_REG_RO},
	{STATUS3_TCRIT2, "Status 3 (TCRIT2)", EMUL_SENSOR_REG_RO},
	{STATUS4_TCRIT3, "Status 4 (TCRIT3)", EMUL_SENSOR_REG_RO},
	{DIODE_MODEL_STATUS, "Diode Model Status", EMUL_SENSOR_REG_RO},

	{TCRIT1_MASK, "TCRIT1 Mask", .reset = 0x19, .write_mask = 0x1f},
	{TCRIT2_MASK, "TCRIT2 Mask", .write_mask = 0x1f},
	{TCRIT3_MASK, "TCRIT3 Mask", .reset = 0x07, .write_mask = 0x1f},

	/* Bit 7 of the local limit is unimplemented and always reads 0 (0 degC to 127 degC). */
	{LOCAL_TCRIT_LIMIT, "Local Tcrit Limit", .reset = 0x55, .write_mask = 0x7f},
	{REMOTE1_TCRIT1_LIMIT, "Remote 1 Tcrit-1 Limit", .reset = 0x6e, .write_mask = 0xff},
	{REMOTE2_TCRIT1_LIMIT, "Remote 2 Tcrit-1 Limit", .reset = 0x6e, .write_mask = 0xff},
	{REMOTE3_TCRIT_LIMIT, "Remote 3 Tcrit Limit", .reset = 0x55, .write_mask = 0xff},
	{REMOTE4_TCRIT_LIMIT, "Remote 4 Tcrit Limit", .reset = 0x55, .write_mask = 0xff},
	{REMOTE1_TCRIT23_LIMIT, "Remote 1 Tcrit-2 and Tcrit-3 Limit", .reset = 0x55,
	 .write_mask = 0xff},
	{REMOTE2_TCRIT23_LIMIT, "Remote 2 Tcrit-2 and Tcrit-3 Limit", .reset = 0x55,
	 .write_mask = 0xff},
	{COMMON_TCRIT_HYSTERESIS, "Common Tcrit Hysteresis", .reset = 0x0a, .write_mask = 0x1f},

	{MANUFACTURER_ID, "Manufacturer ID", EMUL_SENSOR_REG_RO, .reset = 0x01},
	{REVISION_ID, "Revision ID", EMUL_SENSOR_REG_RO, .reset = 0x79},
};

/*
 * Each temperature is a left-justified word split across two non-consecutive one-byte
 * registers: an 11-bit 2's complement code (10-bit magnitude plus sign) at 0.125 degC/LSB
 * (Tables 2, 3 and 6), or, with the digital filter enabled on remote 1 or 2, a 13-bit code
 * (12-bit magnitude plus sign) at 0.03125 degC/LSB (Table 4, 5). The sign bit is always D7 of
 * the MSB register; the fraction bits occupy the top of the LSB register, and any bits below
 * the resolution in use read 0. Because the MSB/LSB pair, and for remote channels the unsigned
 * alias pair, are not consecutive addresses, the split is done in sample() below instead of
 * through the automatic multi-register packing. Local temperature has no unsigned alias and no
 * offset register; remote channels have both.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = LOCAL_TEMP_MSB, .is_signed = true, .bits = 11,
	 .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875,
	 .disabled = {CHANNEL_CONVERSION_ENABLE, BIT(0), 0}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = REMOTE1_TEMP_MSB, .is_signed = true,
	 .min = -128, .max = 127.875,
	 .select = {FILTER_SETTING, GENMASK(1, 0)},
	 .variants = {{.bits = 11, .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875},
		      {.bits = 13, .pos = 3, .lsb = 0.03125, .min = -128, .max = 127.96875},
		      {.bits = 11, .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875},
		      {.bits = 13, .pos = 3, .lsb = 0.03125, .min = -128, .max = 127.96875}},
	 .disabled = {CHANNEL_CONVERSION_ENABLE, BIT(1), 0}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = REMOTE2_TEMP_MSB, .is_signed = true,
	 .min = -128, .max = 127.875,
	 .select = {FILTER_SETTING, GENMASK(3, 2)},
	 .variants = {{.bits = 11, .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875},
		      {.bits = 13, .pos = 3, .lsb = 0.03125, .min = -128, .max = 127.96875},
		      {.bits = 11, .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875},
		      {.bits = 13, .pos = 3, .lsb = 0.03125, .min = -128, .max = 127.96875}},
	 .disabled = {CHANNEL_CONVERSION_ENABLE, BIT(2), 0}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = REMOTE3_TEMP_MSB, .is_signed = true, .bits = 11,
	 .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875,
	 .disabled = {CHANNEL_CONVERSION_ENABLE, BIT(3), 0}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = REMOTE4_TEMP_MSB, .is_signed = true, .bits = 11,
	 .pos = 5, .lsb = 0.125, .min = -128, .max = 127.875,
	 .disabled = {CHANNEL_CONVERSION_ENABLE, BIT(4), 0}},
};

/* Round to the nearest count, halfway away from zero. */
static int32_t round_counts(double x)
{
	return (int32_t)(x >= 0 ? x + 0.5 : x - 0.5);
}

/*
 * Pack a bits-wide 2's complement code at bit position pos of a 16-bit word (bits + pos is
 * always 16, so the code exactly fills the word with no sign extension needed), split it into
 * the MSB/LSB register pair, and mirror the same raw bytes into the unsigned alias pair if the
 * channel has one: the unsigned registers hold the identical ADC code (see the file comment),
 * not a separately computed value.
 */
static void store_temp(struct emul_sensor_regmap_data *data, int32_t code, uint8_t bits,
			uint8_t pos, uint8_t msb_reg, uint8_t lsb_reg, uint8_t msb_u_reg,
			uint8_t lsb_u_reg)
{
	uint16_t raw = (uint16_t)(((uint32_t)code & GENMASK(bits - 1, 0)) << pos);

	data->regs[msb_reg] = (raw >> 8) & 0xffU;
	data->regs[lsb_reg] = raw & 0xffU;
	if (msb_u_reg != 0U) {
		data->regs[msb_u_reg] = data->regs[msb_reg];
		data->regs[lsb_u_reg] = data->regs[lsb_reg];
	}
}

/*
 * Diode fault detection (open/shorted D+/D-) is not modeled: it depends on the analog state of
 * a pin the emulator has no channel for, so Status 1 stays at its reset value of 0 and remote
 * temperature readings are never forced to -128 degC / 0 degC for a fault.
 */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;
	int idx;
	uint8_t msb, lsb, msb_u = 0, lsb_u = 0, offset_reg = 0;
	uint8_t bits = 11, pos = 5;
	double lsb_val = 0.125;
	int32_t code;
	int32_t min_code, max_code;

	switch (addr) {
	case LOCAL_TEMP_MSB:
		idx = CH_LOCAL;
		msb = LOCAL_TEMP_MSB;
		lsb = LOCAL_TEMP_LSB;
		break;
	case REMOTE1_TEMP_MSB:
	case REMOTE2_TEMP_MSB:
	case REMOTE3_TEMP_MSB:
	case REMOTE4_TEMP_MSB:
		if (addr == REMOTE1_TEMP_MSB) {
			idx = CH_REMOTE1;
			msb = REMOTE1_TEMP_MSB;
			lsb = REMOTE1_TEMP_LSB;
			msb_u = REMOTE1_TEMP_MSB_U;
			lsb_u = REMOTE1_TEMP_LSB_U;
			offset_reg = REMOTE1_OFFSET;
		} else if (addr == REMOTE2_TEMP_MSB) {
			idx = CH_REMOTE2;
			msb = REMOTE2_TEMP_MSB;
			lsb = REMOTE2_TEMP_LSB;
			msb_u = REMOTE2_TEMP_MSB_U;
			lsb_u = REMOTE2_TEMP_LSB_U;
			offset_reg = REMOTE2_OFFSET;
		} else if (addr == REMOTE3_TEMP_MSB) {
			idx = CH_REMOTE3;
			msb = REMOTE3_TEMP_MSB;
			lsb = REMOTE3_TEMP_LSB;
			msb_u = REMOTE3_TEMP_MSB_U;
			lsb_u = REMOTE3_TEMP_LSB_U;
			offset_reg = REMOTE3_OFFSET;
		} else {
			idx = CH_REMOTE4;
			msb = REMOTE4_TEMP_MSB;
			lsb = REMOTE4_TEMP_LSB;
			msb_u = REMOTE4_TEMP_MSB_U;
			lsb_u = REMOTE4_TEMP_LSB_U;
			offset_reg = REMOTE4_OFFSET;
		}

		/* Only remote 1 and 2 have a digital filter; treat "10" (reserved) as off. */
		if (addr == REMOTE1_TEMP_MSB || addr == REMOTE2_TEMP_MSB) {
			uint8_t field = data->regs[FILTER_SETTING];

			field = addr == REMOTE1_TEMP_MSB ? (field & GENMASK(1, 0))
							  : (field >> 2) & GENMASK(1, 0);
			if (field == 0x01U || field == 0x03U) {
				bits = 13;
				pos = 3;
				lsb_val = 0.03125;
			}
		}
		break;
	default:
		return true;
	}

	code = round_counts(data->inputs[idx] / lsb_val);

	if (offset_reg != 0U) {
		int32_t off = (int8_t)(data->regs[offset_reg] & 0xffU);

		code += round_counts((off * 0.5) / lsb_val);
	}

	max_code = (int32_t)BIT(bits - 1) - 1;
	min_code = -(int32_t)BIT(bits - 1);
	code = CLAMP(code, min_code, max_code);

	store_temp(data, code, bits, pos, msb, lsb, msb_u, lsb_u);

	ARG_UNUSED(value);
	return false;
}

/*
 * The device converts continuously whenever it is not in standby; clearing STBY resumes
 * continuous conversion without STBY itself being written as one, so convert_on_write cannot
 * express it (as with the STTS751 and TMP451 models). Enabling a previously disabled channel,
 * changing the digital filter, or changing an offset likewise take effect on the next
 * conversion, which this model performs immediately.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	switch (reg) {
	case CONFIGURATION:
		if ((old & BIT(6)) != 0U && (data->regs[CONFIGURATION] & BIT(6)) == 0U) {
			emul_sensor_regmap_convert(target);
		}
		break;
	case CHANNEL_CONVERSION_ENABLE:
		if ((~old & data->regs[CHANNEL_CONVERSION_ENABLE] & 0x1fU) != 0U) {
			emul_sensor_regmap_convert(target);
		}
		break;
	case FILTER_SETTING:
	case REMOTE1_OFFSET:
	case REMOTE2_OFFSET:
	case REMOTE3_OFFSET:
	case REMOTE4_OFFSET:
		emul_sensor_regmap_convert(target);
		break;
	default:
		break;
	}
}

/* The Command Register pointer is retained across STOP (section "Communicating with the..."). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = BIT(6), .value = BIT(6)},
	.write = write, .sample = sample);
