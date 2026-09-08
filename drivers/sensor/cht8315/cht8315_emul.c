/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT sensylink_cht8315

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * CHT8315, Sep. 2023 Rev. 1.6, section 1.1 Register map (Table 1):
 * https://raw.githubusercontent.com/pvvx/pvvx.github.io/master/THB2/CHT8315%20Advanced%20Datasheet%20Ver1.6%2020230927.pdf
 *
 * All 12 registers are 2 bytes (MSB then LSB on the bus, figures 5-6). A write sends the
 * pointer byte once per transaction and the pointer is not shown incrementing across a STOP,
 * so the model keeps it fixed. SMBus PEC (section 1.4.6) is not modeled.
 */
enum {
	TEMPERATURE = 0x00,
	HUMIDITY = 0x01,
	STATUS = 0x02,
	CONFIGURE = 0x03,
	CONVERT_RATE = 0x04,
	TEMPERATURE_HIGH_LIMIT = 0x05,
	TEMPERATURE_LOW_LIMIT = 0x06,
	HUMIDITY_HIGH_LIMIT = 0x07,
	HUMIDITY_LOW_LIMIT = 0x08,
	ONESHOT = 0x0f,
	SWRST = 0xfc,
	MANUFACTURE_ID = 0xfe,
	DEVICE_ID = 0xff,
};

/*
 * CONFIGURE (0x03) bits in the combined 16-bit MSB:LSB word (section 1.1.3): Mask(15), SD(14)
 * standby, ALTH(13) hysteresis mode, EM(12) 13-/14-bit temperature format, FAST(9), EHT(8)
 * heater, TME(7) SMBus timeout enable, POL(5), ALT_SRC[4:3], CONSEC_FQ[2:1], ATM(0). Only SD
 * and EM affect measurement encoding; the rest are stored but not otherwise modeled.
 */
#define CONFIGURE_SD BIT(14)
#define CONFIGURE_EM BIT(12)

static const struct emul_sensor_reg registers[] = {
	{TEMPERATURE, "Temperature", EMUL_SENSOR_REG_RO},
	{HUMIDITY, "Humidity", EMUL_SENSOR_REG_RO},
	/* Busy=bit15, THIGH=bit14, TLOW=bit13, HHigh=bit12, HLow=bit11; not driven by the model. */
	{STATUS, "Status", EMUL_SENSOR_REG_RO},
	/*
	 * Table 1 lists reset 0x0880: TME defaults to 1 as documented in section 1.1.3, but bit11
	 * (one of the two reserved/don't-care bits of the MSB byte) also reads 1, which the text
	 * does not mention; the numeric reset value is followed as-is. Bit6 of the LSB byte is
	 * reserved and must stay 0, unlike the two "x" (don't care) bits of the MSB byte.
	 */
	{CONFIGURE, "Configure", .reset = 0x0880, .write_mask = 0xffbf},
	/* CR[2:0] is bits 10:8; reset 0x0400 is CR=100b, 1 conversion/s (Table 7). */
	{CONVERT_RATE, "Convert rate", .reset = 0x0400, .write_mask = GENMASK(10, 8)},
	/* TH[12:0] in bits 15:3, same encoding as Temperature; reset 0x5500 is +85.0 degC. */
	{TEMPERATURE_HIGH_LIMIT, "Temperature high limit", .reset = 0x5500,
	 .write_mask = GENMASK(15, 3)},
	/* reset 0xD800 is -40.0 degC. */
	{TEMPERATURE_LOW_LIMIT, "Temperature low limit", .reset = 0xd800,
	 .write_mask = GENMASK(15, 3)},
	/*
	 * HH[14:8] in bits 14:8 only, comparing the top 7 bits of the 15-bit humidity code; the
	 * low 8 bits and bit 15 are reserved. Reset 0xD900 sets bit15, which the table marks
	 * reserved (0); the numeric reset value is followed as-is.
	 */
	{HUMIDITY_HIGH_LIMIT, "Humidity high limit", .reset = 0xd900,
	 .write_mask = GENMASK(14, 8)},
	{HUMIDITY_LOW_LIMIT, "Humidity low limit", .write_mask = GENMASK(14, 8)},
	/* Write-only, value is a don't care including 0x0000 (section 1.1.6); handled in write(). */
	{ONESHOT, "Oneshot", .reset = 0xffff, .write_mask = 0xffff, .convert_on_write = 0xffff,
	 .requires_standby = true, .self_clear = 0xffff},
	/* Write-only, value is a don't care; any write resets all registers, handled in write(). */
	{SWRST, "SWRST", .reset = 0xffff, .write_mask = 0xffff},
	{MANUFACTURE_ID, "Manufacture ID", EMUL_SENSOR_REG_RO, .reset = 0x5959},
	/*
	 * Section 1.1.8 documents the register but never states its value, only that it holds the
	 * released version or part number. 0x8315 is the part number.
	 */
	{DEVICE_ID, "Device ID", EMUL_SENSOR_REG_RO, .reset = 0x8315},
};

/*
 * Table 4: two's complement value in bits 15:3 (EM=0, 13-bit) or 15:2 (EM=1, 14-bit) of the
 * Temperature register, 0.03125 degC/LSB either way (Temperature Resolution, electrical
 * characteristics). EM is CONFIGURE bit 12.
 *
 * Table 4 (16-bit humidity data): HD[14:0] unsigned in bits 14:0 of the Humidity register, bit
 * 15 (OL) is an overflow flag not modeled; Relative Humidity = 100% * HD[14:0] / 2^15.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMPERATURE, .is_signed = true,
	 .min = -40, .max = 125,
	 .select = {.reg = CONFIGURE, .mask = CONFIGURE_EM},
	 .variants = {{.bits = 13, .pos = 3, .lsb = 0.03125},
		      {.bits = 14, .pos = 2, .lsb = 0.03125}}},
	{.chan = SENSOR_CHAN_HUMIDITY, .reg = HUMIDITY, .bits = 15, .pos = 0,
	 .lsb = 100.0 / 32768, .min = 0, .max = 100},
};

/*
 * The one-shot command is accepted with any written value while in standby (section 1.1.6),
 * including all zero, which convert_on_write cannot express by itself. A soft reset (SWRST) is
 * likewise triggered by any write. Leaving standby (SD 1 -> 0) does not by itself resume
 * conversion of the current input until the next 1 Hz (default) cycle; force one immediately so
 * a fetch right after clearing SD sees the retained input rather than a stale conversion.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == SWRST) {
		emul_sensor_regmap_reset(target);
	} else if (reg == ONESHOT && (data->regs[CONFIGURE] & CONFIGURE_SD) != 0U) {
		emul_sensor_regmap_convert(target);
	} else if (reg == CONFIGURE && (old & CONFIGURE_SD) != 0U &&
		   (data->regs[CONFIGURE] & CONFIGURE_SD) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true, .disabled = {.reg = CONFIGURE, .mask = CONFIGURE_SD,
					     .value = CONFIGURE_SD},
	.write = write);
