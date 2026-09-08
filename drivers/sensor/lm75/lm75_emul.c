/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT lm75

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * LM75B, Rev. 6.1 - 6 February 2015, section 7.4 "Register list" (Table 5) and section 7.4.1
 * "Pointer register" (Tables 6-7): https://www.nxp.com/docs/en/data-sheet/LM75B.pdf
 */
enum { TEMP = 0x00, CONF = 0x01, THYST = 0x02, TOS = 0x03 };

static const struct emul_sensor_reg registers[] = {
	{TEMP, "Temp", EMUL_SENSOR_REG_RO, .bytes = 2},
	/* B[7:5] reserved, kept at 0; B[4:3] OS_F_QUE, B2 OS_POL, B1 OS_COMP_INT, B0 SHUTDOWN */
	{CONF, "Conf", .bytes = 1, .reset = 0x00, .write_mask = 0x1f},
	{THYST, "Thyst", .bytes = 2, .reset = 0x4b00, .write_mask = 0xff80},
	{TOS, "Tos", .bytes = 2, .reset = 0x5000, .write_mask = 0xff80},
};

/*
 * Temp is an 11-bit two's complement value in the 9 MSBits of the MSByte and the 3 MSbits of
 * the LSByte (Table 9); the 5 LSbits of the LSByte read 0. 1 LSB = 0.125 degC (section 7.4.3).
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .is_signed = true, .bits = 11, .pos = 5,
	 .lsb = 0.125,
	 .min = -55, .max = 125},
};

/*
 * Conversion runs continuously every 100 ms in normal mode and is stopped, with the Temp
 * register holding its last result, in shutdown; it restarts as soon as shutdown is cleared
 * (section 7.1, section 7.8). convert_on_write only fires on a bit written as one, so the
 * restart on the SHUTDOWN bit going 1 -> 0 needs an explicit callback.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == CONF && (old & BIT(0)) != 0U && (data->regs[CONF] & BIT(0)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.disabled = {.reg = CONF, .mask = BIT(0), .value = BIT(0)}, .write = write);
