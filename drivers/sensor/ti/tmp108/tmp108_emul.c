/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_tmp108

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SBOS663A - APRIL 2013 - REVISED SEPTEMBER 2019, section 7.5 Programming (7.5.1 Pointer
 * Register, 7.5.2 Temperature Register, 7.5.3 Configuration Register, 7.5.4 High- and
 * Low-Limit Registers): https://www.ti.com/lit/ds/symlink/tmp108.pdf
 *
 * The pointer register (Table 3) only decodes bits P1:P0; Table 4 lists the four resulting
 * addresses. Every register is 16 bits, most significant byte first (section 7.3.4).
 */
enum {
	TEMPERATURE = 0x00,	/* Temperature register (read only, default) */
	CONFIGURATION = 0x01,	/* Configuration register (read/write) */
	T_LOW = 0x02,		/* TLOW register (read/write) */
	T_HIGH = 0x03,		/* THIGH register (read/write) */
};

/* Configuration register bit fields (Table 8). */
#define CONFIG_ID   BIT(15)	/* Undocumented; reserved, not user-writable */
#define CONFIG_CR1  BIT(14)
#define CONFIG_CR0  BIT(13)
#define CONFIG_FH   BIT(12)	/* Set when temperature exceeded THIGH */
#define CONFIG_FL   BIT(11)	/* Set when temperature fell below TLOW */
#define CONFIG_TM   BIT(10)	/* Thermostat mode: 0 comparator, 1 interrupt */
#define CONFIG_M1   BIT(9)
#define CONFIG_M0   BIT(8)
#define CONFIG_POL  BIT(7)
#define CONFIG_HYS1 BIT(5)
#define CONFIG_HYS0 BIT(4)
#define CONFIG_M    (CONFIG_M1 | CONFIG_M0)

static const struct emul_sensor_reg registers[] = {
	{TEMPERATURE, "Temperature register", EMUL_SENSOR_REG_RO},
	/*
	 * M1:M0 = 00 shutdown, 1x continuous (Table 10 sets the rate), 01 one-shot (only
	 * accepted from shutdown, section 7.4.2); the field self-clears to 00 once the
	 * (instantaneous) conversion completes, which is also how a driver polling M1:M0
	 * for one-shot completion is guaranteed to see it clear. Writing M1 as 1 out of
	 * shutdown, or M0 as 1 from shutdown, both start a conversion (section 7.4).
	 * FH and FL (bits 12:11) are status flags set by the device from the comparison
	 * against THIGH/TLOW; the master cannot write them, hence their exclusion below.
	 * ID (bit 15) is not described anywhere in the register map and is treated as
	 * reserved. Reset value from Table 8: byte 1 = 0x22 (CR0=1, M1=1), byte 2 = 0x10
	 * (HYS0=1), i.e. continuous conversion at 1 Hz with 1 degC hysteresis.
	 */
	{CONFIGURATION, "Configuration register", .bytes = 2, .reset = 0x2210,
	 .write_mask = CONFIG_CR1 | CONFIG_CR0 | CONFIG_TM | CONFIG_M | CONFIG_POL |
		       CONFIG_HYS1 | CONFIG_HYS0,
	 .self_clear = CONFIG_M0, .convert_on_write = CONFIG_M, .requires_standby = true},
	/*
	 * Table 12: 12-bit signed value in bits 15:4, bits 3:0 always read 0. Reset -128 degC
	 * = 0x8000 (Table 8.5.4 text, consistent with the 12-bit code 0x800 left-justified).
	 */
	{T_LOW, "TLOW register", .bytes = 2, .reset = 0x8000, .write_mask = 0xfff0},
	/*
	 * Table 11: same format. The reset value is documented in prose as +127.9375 degC
	 * (0x7FF8), but that hex value has bit 3 set, which Table 11 defines as always 0; the
	 * 12-bit code for +127.9375 degC (0x7FF, the same full-scale code as Table 7) left
	 * justified into bits 15:4 is 0x7FF0, which this model uses.
	 */
	{T_HIGH, "THIGH register", .bytes = 2, .reset = 0x7ff0, .write_mask = 0xfff0},
};

/* Section 6.5 Electrical Characteristics, Temperature Input Range. */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMPERATURE, .is_signed = true, .bits = 12,
	 .pos = 4, .lsb = 0.0625,
	 .min = -40, .max = 125},
};

enum { CH_TEMP = 0 };

/*
 * FH and FL (section 7.5.4, Figures 13-14) compare each conversion against THIGH/TLOW.
 * In comparator mode (TM=0) they track the hysteresis window (TLOW + HYS, THIGH - HYS) and
 * clear themselves once the temperature returns inside it. In interrupt mode (TM=1) they only
 * latch to 1 and stay there until the configuration register is read (handled in read() below);
 * they are never auto-cleared by a later conversion. Both registers use the same 12-bit,
 * 0.0625 degC/LSB format as the temperature register, so their raw contents are decoded here
 * rather than relying on the encoded sample value, whose framework-side format is not part of
 * this callback's contract.
 */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;
	static const double hys_table[4] = {0.0, 1.0, 2.0, 4.0};
	uint32_t config = data->regs[CONFIGURATION];
	double hys = hys_table[(config & (CONFIG_HYS1 | CONFIG_HYS0)) >> 4];
	double thigh = ((int16_t)data->regs[T_HIGH] >> 4) * 0.0625;
	double tlow = ((int16_t)data->regs[T_LOW] >> 4) * 0.0625;
	double temp = data->inputs[CH_TEMP];
	bool fh = (config & CONFIG_FH) != 0U;
	bool fl = (config & CONFIG_FL) != 0U;

	ARG_UNUSED(value);
	if (addr != TEMPERATURE) {
		return true;
	}

	if (temp > thigh) {
		fh = true;
	} else if ((config & CONFIG_TM) == 0U && temp <= thigh - hys) {
		fh = false;
	}
	if (temp < tlow) {
		fl = true;
	} else if ((config & CONFIG_TM) == 0U && temp >= tlow + hys) {
		fl = false;
	}

	data->regs[CONFIGURATION] = (config & ~(CONFIG_FH | CONFIG_FL)) |
				     (fh ? CONFIG_FH : 0U) | (fl ? CONFIG_FL : 0U);
	return true;
}

/*
 * Section 7.5.4: in interrupt mode, reading the configuration register clears FH and FL (and
 * the ALERT pin). Comparator mode leaves them alone; there they track the hysteresis window
 * continuously and a read must not disturb that state. The SMBus alert response and the
 * general-call reset also clear the flags, but both address the device on an address other
 * than its own and are not representable by this per-instance register map.
 */
static void read(const struct emul *target, uint8_t addr)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == CONFIGURATION && (data->regs[CONFIGURATION] & CONFIG_TM) != 0U) {
		data->regs[CONFIGURATION] &= ~(CONFIG_FH | CONFIG_FL);
	}
}

/* Shutdown is M1:M0 = 00 (section 7.4.1); the pointer register is retained across STOP. */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true, .disabled = {.reg = CONFIGURATION, .mask = CONFIG_M, .value = 0},
	.sample = sample, .read = read);
