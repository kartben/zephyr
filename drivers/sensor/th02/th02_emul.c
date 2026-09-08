/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT hoperf_th02

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * TH02 datasheet, HOPERF (Hope Microelectronics), version V1.1, 2013-08,
 * sections 3 "Host Interface" (pages 15-20) and 5 "Control Registers" (pages 22-24):
 * http://www.hoperf.com/upload/sensor/TH02_V1.1.pdf
 *
 * Any register address not listed in Table 20 is reserved and must not be written; the
 * device NAKs or ignores transfers to it (section 5), modeled by simply not listing it here.
 */
enum {
	STATUS = 0x00,
	DATAH = 0x01,
	DATAL = 0x02,
	CONFIG = 0x03,
	ID = 0x11,
};

/* CONFIG (register 3) bit positions, Table 20 / section 5.1. */
#define CONFIG_FAST  BIT(5)
#define CONFIG_TEMP  BIT(4)
#define CONFIG_HEAT  BIT(1)
#define CONFIG_START BIT(0)

static const struct emul_sensor_reg registers[] = {
	/* D0 = /RDY: 0 = conversion complete, 1 = conversion in progress (register 0). */
	/*
	 * /RDY powers up set (conversion in progress). Conversions are instantaneous here, so it
	 * resets clear; a driver polling it before the first injection would otherwise spin.
	 */
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO},
	{DATAH, "DATAh", EMUL_SENSOR_REG_RO},
	{DATAL, "DATAI", EMUL_SENSOR_REG_RO},
	/* FAST, TEMP, HEAT and START; bits 7:6 and 3:2 are reserved and read undefined. */
	{CONFIG, "CONFIG", .write_mask = CONFIG_FAST | CONFIG_TEMP | CONFIG_HEAT | CONFIG_START,
	 .convert_on_write = CONFIG_START},
	/* D[7:4] = device type (0101 = TH02), D[3:0] = revision level (0000 = rev B). */
	{ID, "ID", EMUL_SENSOR_REG_RO, .reset = 0x50},
};

/*
 * DATAh:DATAI (registers 1-2) form one 16-bit, big-endian, unsigned word shared by the
 * humidity and temperature results; CONFIG.TEMP (D4) selects which measurement the next
 * conversion produces (section 3.1.1/3.1.2), modeled with a channel .disabled condition on
 * that bit so only the selected channel's conversion is written to the shared registers.
 *
 * Humidity: 12-bit code in bits 15:4 of the word, D3:D0 of DATAI read 0 (Table 10).
 * %RH = (code / 16) - 24 (section 3.1.1), so 0.0625 %RH/LSB, offset -24 %RH at code 0.
 * Verified against Table 11: code 1184 (0x4A0) -> 1184/16 - 24 = 50 %RH.
 *
 * Temperature: 14-bit code in bits 15:2 of the word, D1:D0 of DATAI read 0 (Table 12).
 * degC = (code / 32) - 50 (section 3.1.2), so 0.03125 degC/LSB, offset -50 degC at code 0.
 * Verified against Table 13: code 2880 (0x0B40) -> 2880/32 - 50 = 40 degC.
 *
 * CONFIG.FAST (D5) halves the conversion time and drops one bit of resolution on whichever
 * measurement is selected (Table 14: 12-bit -> 11-bit RH, 14-bit -> 13-bit temperature); no
 * fast-mode code table is given, so the fast variants follow the same one-bit-less encoding
 * used across the field (LSB doubles, code position moves up by one), keeping the same
 * physical range and offset as the corresponding normal-mode measurement.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_HUMIDITY, .reg = DATAH, .whole_word = true,
	 .offset = -24, .min = 0, .max = 100,
	 .select = {.reg = CONFIG, .mask = CONFIG_FAST},
	 .variants = {{.bits = 12, .pos = 4, .lsb = 0.0625},
		      {.bits = 11, .pos = 5, .lsb = 0.125}},
	 .disabled = {.reg = CONFIG, .mask = CONFIG_TEMP, .value = CONFIG_TEMP}},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = DATAH, .whole_word = true,
	 .offset = -50, .min = -40, .max = 85,
	 .select = {.reg = CONFIG, .mask = CONFIG_FAST},
	 .variants = {{.bits = 14, .pos = 2, .lsb = 0.03125},
		      {.bits = 13, .pos = 3, .lsb = 0.0625}},
	 .disabled = {.reg = CONFIG, .mask = CONFIG_TEMP, .value = 0}},
};

/*
 * /RDY (STATUS D0) is active low, the opposite polarity of the framework's .ready field
 * (which only sets bits), so it is cleared here instead. CONFIG.START's convert_on_write
 * already produced the sample by the time this runs; the conversion is instantaneous in this
 * model, so the flag is cleared in the same transaction that started it (section 3.1.1/3.1.2
 * describe polling RDY after writing START, not a minimum delay before it can read 0).
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (addr == CONFIG && (data->regs[CONFIG] & CONFIG_START) != 0U) {
		data->regs[STATUS] &= ~(uint32_t)BIT(0);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .write = write);
