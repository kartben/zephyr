/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_tmp451

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SBOS686A - JUNE 2013 - REVISED DECEMBER 2014, section 7.6 Register Map:
 * https://www.ti.com/lit/ds/symlink/tmp451.pdf
 *
 * For backward compatibility with older local/remote diode sensors, six registers are read at
 * one pointer address and written at a different one (Table 3, sections 7.6.1.6 to 7.6.1.11):
 * the pair below the corresponding "_R"/"_W" name is the same physical register. All other
 * registers share one address for both directions.
 */
enum {
	LT = 0x00,			/* Local temperature, high byte */
	RT = 0x01,			/* Remote temperature, high byte */
	STATUS = 0x02,
	CONFIG_R = 0x03,
	CONV_RATE_R = 0x04,
	LOCAL_HIGH_LIMIT_R = 0x05,
	LOCAL_LOW_LIMIT_R = 0x06,
	REMOTE_HIGH_LIMIT_HIGH_R = 0x07,
	REMOTE_LOW_LIMIT_HIGH_R = 0x08,
	CONFIG_W = 0x09,
	CONV_RATE_W = 0x0a,
	LOCAL_HIGH_LIMIT_W = 0x0b,
	LOCAL_LOW_LIMIT_W = 0x0c,
	REMOTE_HIGH_LIMIT_HIGH_W = 0x0d,
	REMOTE_LOW_LIMIT_HIGH_W = 0x0e,
	ONE_SHOT = 0x0f,
	REMOTE_TEMP_LOW = 0x10,
	REMOTE_OFFSET_HIGH = 0x11,
	REMOTE_OFFSET_LOW = 0x12,
	REMOTE_HIGH_LIMIT_LOW = 0x13,
	REMOTE_LOW_LIMIT_LOW = 0x14,
	LOCAL_TEMP_LOW = 0x15,
	REMOTE_THERM_LIMIT = 0x19,
	LOCAL_THERM_LIMIT = 0x20,
	THERM_HYSTERESIS = 0x21,
	CONSECUTIVE_ALERT = 0x22,
	NFACTOR_CORRECTION = 0x23,
	DIGITAL_FILTER = 0x24,
	MANUFACTURER_ID = 0xfe,
};

/* Channel indices, matching the order of the channels[] array below. */
enum { CH_LOCAL = 0, CH_REMOTE = 1 };

static const struct emul_sensor_reg registers[] = {
	{LT, "Local temperature high byte", EMUL_SENSOR_REG_RO},
	{RT, "Remote temperature high byte", EMUL_SENSOR_REG_RO},
	{STATUS, "Status", EMUL_SENSOR_REG_RO},
	{CONFIG_R, "Configuration (read)", EMUL_SENSOR_REG_RO},
	{CONV_RATE_R, "Conversion rate (read)", EMUL_SENSOR_REG_RO, .reset = 0x08},
	{LOCAL_HIGH_LIMIT_R, "Local temperature high limit (read)", EMUL_SENSOR_REG_RO,
	 .reset = 0x55},
	{LOCAL_LOW_LIMIT_R, "Local temperature low limit (read)", EMUL_SENSOR_REG_RO},
	{REMOTE_HIGH_LIMIT_HIGH_R, "Remote temperature high limit high byte (read)",
	 EMUL_SENSOR_REG_RO, .reset = 0x55},
	{REMOTE_LOW_LIMIT_HIGH_R, "Remote temperature low limit high byte (read)",
	 EMUL_SENSOR_REG_RO},
	/* MASK1, SD, ALERT/THERM2 and RANGE; bits 4, 3, 1 and 0 are reserved */
	{CONFIG_W, "Configuration (write)", .write_mask = 0xe4},
	{CONV_RATE_W, "Conversion rate (write)", .reset = 0x08, .write_mask = 0x0f},
	{LOCAL_HIGH_LIMIT_W, "Local temperature high limit (write)", .reset = 0x55,
	 .write_mask = 0xff},
	{LOCAL_LOW_LIMIT_W, "Local temperature low limit (write)", .write_mask = 0xff},
	{REMOTE_HIGH_LIMIT_HIGH_W, "Remote temperature high limit high byte (write)",
	 .reset = 0x55, .write_mask = 0xff},
	{REMOTE_LOW_LIMIT_HIGH_W, "Remote temperature low limit high byte (write)",
	 .write_mask = 0xff},
	/* The value written is a don't care (section 7.4.2) */
	{ONE_SHOT, "One-shot start", .self_clear = 0xff, .convert_on_write = 0xff,
	 .requires_standby = true},
	{REMOTE_TEMP_LOW, "Remote temperature low byte", EMUL_SENSOR_REG_RO},
	{REMOTE_OFFSET_HIGH, "Remote temperature offset high byte", .write_mask = 0xff},
	{REMOTE_OFFSET_LOW, "Remote temperature offset low byte", .write_mask = 0xf0},
	{REMOTE_HIGH_LIMIT_LOW, "Remote temperature high limit low byte", .write_mask = 0xf0},
	{REMOTE_LOW_LIMIT_LOW, "Remote temperature low limit low byte", .write_mask = 0xf0},
	{LOCAL_TEMP_LOW, "Local temperature low byte", EMUL_SENSOR_REG_RO},
	{REMOTE_THERM_LIMIT, "Remote temperature THERM limit", .reset = 0x6c,
	 .write_mask = 0xff},
	{LOCAL_THERM_LIMIT, "Local temperature THERM limit", .reset = 0x55, .write_mask = 0xff},
	{THERM_HYSTERESIS, "THERM hysteresis", .reset = 0x0a, .write_mask = 0xff},
	/* SMBTO and CONAL2:0; bit 0 reads 1 at reset but is reserved */
	{CONSECUTIVE_ALERT, "Consecutive ALERT", .reset = 0x01, .write_mask = 0x8e},
	{NFACTOR_CORRECTION, "eta-factor correction", .write_mask = 0xff},
	{DIGITAL_FILTER, "Digital filter control", .write_mask = 0x03},
	{MANUFACTURER_ID, "Manufacturer ID", EMUL_SENSOR_REG_RO, .reset = 0x55},
};

/*
 * Local and remote temperature are each a 12-bit code across two non-consecutive registers:
 * an 8-bit integer part (1 degC/LSB) at the channel's .reg, and a 4-bit fraction in bits 7:4
 * of a register elsewhere in the map (LOCAL_TEMP_LOW / REMOTE_TEMP_LOW), 0.0625 degC/LSB
 * (Table 1, Table 2, sections 7.6.1.3 to 7.6.1.13). In standard binary (RANGE = 0) the code is
 * the temperature directly, clamped to 0 - 127 degC. In extended binary (RANGE = 1) a -64 degC
 * offset is added, giving -64 to 191 degC (section 7.3.1). Because struct emul_sensor_field has
 * no per-variant offset, the RANGE-dependent encoding is computed in sample() below instead of
 * through .select/.variants, which are kept only to publish the two ranges.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = LT, .bits = 12, .pos = 4, .lsb = 0.0625,
	 .min = 0, .max = 127,
	 .select = {CONFIG_R, BIT(2)},
	 .variants = {{.min = 0, .max = 127}, {.min = -64, .max = 191}}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = RT, .bits = 12, .pos = 4, .lsb = 0.0625,
	 .min = 0, .max = 127,
	 .select = {CONFIG_R, BIT(2)},
	 .variants = {{.min = 0, .max = 127}, {.min = -64, .max = 191}}},
};

/* Round to the nearest count, halfway away from zero. */
static int32_t round_counts(double x)
{
	return (int32_t)(x >= 0 ? x + 0.5 : x - 0.5);
}

/*
 * The high/low byte pair for a channel is not consecutive in the address map (STATUS and
 * REMOTE_TEMP_LOW/REMOTE_OFFSET_HIGH... sit between RT and its low byte, and every other
 * register sits between LT and its low byte), so the split is done here instead of by the
 * automatic consecutive-register packing. The remote channel additionally has the offset
 * register (0x11/0x12, two's complement, same 0.0625 degC/LSB format) added to the converted
 * result every conversion (section 7.6.1.14), which the framework has no other way to express.
 */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool extended = (data->regs[CONFIG_R] & BIT(2)) != 0U;
	int32_t max_code = extended ? 4095 : 2047;
	int32_t code;
	uint8_t hi_reg, lo_reg;

	if (addr != LT && addr != RT) {
		return true;
	}

	code = round_counts(data->inputs[addr == LT ? CH_LOCAL : CH_REMOTE] / 0.0625);
	code += extended ? 1024 : 0;
	code = CLAMP(code, 0, max_code);

	if (addr == RT) {
		int32_t off_hi = (int8_t)(data->regs[REMOTE_OFFSET_HIGH] & 0xffU);
		int32_t off_lo = (data->regs[REMOTE_OFFSET_LOW] >> 4) & 0x0fU;

		code += off_hi * 16 + off_lo;
		code = CLAMP(code, 0, max_code);
	}

	hi_reg = addr == LT ? LT : RT;
	lo_reg = addr == LT ? LOCAL_TEMP_LOW : REMOTE_TEMP_LOW;

	data->regs[hi_reg] = (uint32_t)(code >> 4) & 0xffU;
	data->regs[lo_reg] = (uint32_t)(code << 4) & 0xf0U;

	ARG_UNUSED(value);
	return false;
}

/*
 * Six registers are read at one pointer address and written at another (see the register table
 * comment above); keep the read-side alias in sync with every such write. Coming out of
 * shutdown (SD 1 -> 0) resumes continuous conversion (section 7.4.1) without SD itself being
 * written as one, so convert_on_write cannot express it; detect the transition here instead, as
 * with the STTS751 and VEML7700 models.
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	switch (addr) {
	case CONFIG_W:
		data->regs[CONFIG_R] = data->regs[CONFIG_W];
		if ((old & BIT(6)) != 0U && (data->regs[CONFIG_W] & BIT(6)) == 0U) {
			emul_sensor_regmap_convert(target);
		}
		break;
	case CONV_RATE_W:
		data->regs[CONV_RATE_R] = data->regs[CONV_RATE_W];
		break;
	case LOCAL_HIGH_LIMIT_W:
		data->regs[LOCAL_HIGH_LIMIT_R] = data->regs[LOCAL_HIGH_LIMIT_W];
		break;
	case LOCAL_LOW_LIMIT_W:
		data->regs[LOCAL_LOW_LIMIT_R] = data->regs[LOCAL_LOW_LIMIT_W];
		break;
	case REMOTE_HIGH_LIMIT_HIGH_W:
		data->regs[REMOTE_HIGH_LIMIT_HIGH_R] = data->regs[REMOTE_HIGH_LIMIT_HIGH_W];
		break;
	case REMOTE_LOW_LIMIT_HIGH_W:
		data->regs[REMOTE_LOW_LIMIT_HIGH_R] = data->regs[REMOTE_LOW_LIMIT_HIGH_W];
		break;
	default:
		break;
	}
}

/* The pointer register is retained across STOP (section 7.5.1.4). */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONFIG_R, .mask = BIT(6), .value = BIT(6)},
	.write = write, .sample = sample);
