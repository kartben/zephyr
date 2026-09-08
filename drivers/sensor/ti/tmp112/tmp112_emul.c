/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_tmp112

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * SBOS473L (March 2009, revised July 2024), Section 7.5 Programming, Table 7-6 (pointer
 * addresses), Tables 7-8/7-9 (temperature register), Table 7-10 (configuration register) and
 * Tables 7-12/7-13 (TLOW/THIGH registers): https://www.ti.com/lit/ds/symlink/tmp112.pdf
 */
enum {
	TEMPERATURE = 0x00,
	CONFIGURATION = 0x01,
	TLOW = 0x02,
	THIGH = 0x03,
};

/*
 * Configuration register bits (Table 7-10), byte 1 as the high byte of the 16-bit word: OS(15),
 * R1(14), R0(13), F1(12), F0(11), POL(10), TM(9), SD(8), CR1(7), CR0(6), AL(5), EM(3). R1:R0 are
 * fixed at 11 (12-bit converter) and AL is a read-only comparator status bit; neither is
 * writable. Bits 4 and 2:0 of byte 2 are unimplemented and read 0.
 */
static const struct emul_sensor_reg registers[] = {
	{TEMPERATURE, "Temperature Register", EMUL_SENSOR_REG_RO},
	/* Reset 0110 0000 1010 0000: R1:R0 = 11, CR1:CR0 = 10 (4 Hz default), AL = 1 */
	{CONFIGURATION, "Configuration Register", .reset = 0x60A0, .write_mask = 0x9FC8,
	 .convert_on_write = BIT(15), .requires_standby = true},
	/* Reset +75 degC: 75 / 0.0625 = 1200 = 0x4B0, left-justified in bits 15:4 */
	{TLOW, "TLOW Register", .reset = 0x4B00, .write_mask = 0xFFF0},
	/* Reset +80 degC: 80 / 0.0625 = 1280 = 0x500, left-justified in bits 15:4 */
	{THIGH, "THIGH Register", .reset = 0x5000, .write_mask = 0xFFF0},
};

/*
 * 12-bit two's complement value in bits 15:4 of the register, 0.0625 degC/LSB (Tables 7-8/7-9).
 * Setting EM (Configuration bit 3) switches the temperature, TLOW and THIGH registers to a
 * 13-bit format in bits 15:3, extending the digital range above +128 degC; the specified
 * operating range stays -40 degC to 125 degC either way. Bit 0 of byte 2, which the datasheet
 * uses as a fixed indicator of the active format, is not reproduced; it always reads 0 here.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMPERATURE, .is_signed = true,
	 .whole_word = true, .bits = 12, .pos = 4, .lsb = 0.0625, .min = -40, .max = 125,
	 .select = {CONFIGURATION, BIT(3)},
	 .variants = {{0}, {.bits = 13, .pos = 3}}},
};

/*
 * The device converts continuously whenever SD = 0, at the rate selected by CR1:CR0. Clearing
 * SD only arms that free-running conversion; it is not itself a write of the OS bit, so
 * convert_on_write does not fire. Force one conversion on the SD 1->0 transition so the
 * temperature register reflects the current input as soon as the device leaves shutdown.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == CONFIGURATION && (old & BIT(8)) != 0U &&
	    (data->regs[CONFIGURATION] & BIT(8)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

/*
 * Not modeled: the ALERT pin/comparator and interrupt modes, the F1:F0 fault queue, the SMBus
 * ALERT response address, high-speed mode controller-code detection, the bus timeout reset and
 * the two-wire general-call reset command. These are bus-timing and GPIO behaviors outside the
 * register-map model; AL and the fault-queue bits are stored but have no effect here.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = BIT(8), .value = BIT(8)},
	.write = write);
