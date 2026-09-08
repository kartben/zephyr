/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT maxim_max44009

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * 19-5719; Rev 0; 1/11, Register and Bit Descriptions, Table 1. Register Map:
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max44009.pdf
 */
enum {
	INTERRUPT_STATUS = 0x00,
	INTERRUPT_ENABLE = 0x01,
	CONFIGURATION = 0x02,
	LUX_HIGH_BYTE = 0x03,
	LUX_LOW_BYTE = 0x04,
	UPPER_THRESHOLD_HIGH_BYTE = 0x05,
	LOWER_THRESHOLD_HIGH_BYTE = 0x06,
	THRESHOLD_TIMER = 0x07,
};

static const struct emul_sensor_reg registers[] = {
	{INTERRUPT_STATUS, "Interrupt Status", EMUL_SENSOR_REG_RO, .clear_on_read = BIT(0)},
	{INTERRUPT_ENABLE, "Interrupt Enable", .write_mask = 0x01},
	/* TIM[2:0] = 011 and CONT, MANUAL, CDR = 0 at reset; bits 5 and 4 are not implemented */
	{CONFIGURATION, "Configuration", .reset = 0x03, .write_mask = 0xcf},
	{LUX_HIGH_BYTE, "Lux High Byte", EMUL_SENSOR_REG_RO},
	{LUX_LOW_BYTE, "Lux Low Byte", EMUL_SENSOR_REG_RO},
	{UPPER_THRESHOLD_HIGH_BYTE, "Upper Threshold High Byte", .reset = 0xff, .write_mask = 0xff},
	{LOWER_THRESHOLD_HIGH_BYTE, "Lower Threshold High Byte", .write_mask = 0xff},
	{THRESHOLD_TIMER, "Threshold Timer", .reset = 0xff, .write_mask = 0xff},
};

/*
 * Lux = 2^exponent x mantissa x 0.045. The exponent E[3:0] is bits 7:4 of 0x03, the four most
 * significant mantissa bits M[7:4] are bits 3:0 of 0x03 and M[3:0] are bits 3:0 of 0x04, whose
 * bits 7:4 are not implemented. In the big-endian 0x03/0x04 word the exponent is bits 15:12,
 * M[7:4] is bits 11:8 and M[3:0] is bits 3:0, so the mantissa is not one contiguous field. The
 * model carries M[7:4], one count of which is sixteen mantissa counts: Lux = 2^exponent x
 * mantissa x 0.72 for register 0x03 alone, code 0000 0001 0000 being 0.72 lux. M[3:0] stays
 * zero, which reads as the low-resolution reading of register 0x03. Each exponent doubles the
 * LSB (Table 8). Exponents 0000 to 1110 are readings, 1111 is an overrange condition.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_LIGHT, .reg = LUX_HIGH_BYTE, .bits = 4, .pos = 8, .lsb = 0.72,
	 .max = 188000,
	 .select = {LUX_HIGH_BYTE, 0xf0},
	 .variants = {{.lsb = 0.72, .max = 10.8},
		      {.lsb = 1.44, .max = 21.6},
		      {.lsb = 2.88, .max = 43.2},
		      {.lsb = 5.76, .max = 86.4},
		      {.lsb = 11.52, .max = 172.8},
		      {.lsb = 23.04, .max = 345.6},
		      {.lsb = 46.08, .max = 691.2},
		      {.lsb = 92.16, .max = 1382.4}}},
};

/* INTS is also cleared by writing INTE = 0 (Interrupt Status 0x00). */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == INTERRUPT_ENABLE && (data->regs[INTERRUPT_ENABLE] & BIT(0)) == 0U) {
		data->regs[INTERRUPT_STATUS] &= ~BIT(0);
	}
}

/* The register pointer is loaded by a write before every read; figure 8 re-addresses 0x04. */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.write = write);
