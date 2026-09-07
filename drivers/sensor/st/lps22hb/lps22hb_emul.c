/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lps22hb_press

#include <zephyr/drivers/emul_sensor_regmap.h>

/* DocID027083 Rev. 6, sections 7-9: https://www.st.com/resource/en/datasheet/lps22hb.pdf */
enum {
	INTERRUPT_CFG = 0x0b, THS_P = 0x0c, WHO_AM_I = 0x0f,
	CTRL_REG1 = 0x10, CTRL_REG2 = 0x11, CTRL_REG3 = 0x12, FIFO_CTRL = 0x14,
	REF_P = 0x15, RPDS = 0x18, RES_CONF = 0x1a, INT_SOURCE = 0x25,
	FIFO_STATUS = 0x26, STATUS = 0x27, PRESS_OUT = 0x28, TEMP_OUT = 0x2b, LPFP_RES = 0x33,
};

static const struct emul_sensor_reg registers[] = {
	{INTERRUPT_CFG, "INTERRUPT_CFG", .bytes = 1, .write_mask = 0xff},
	{THS_P, "THS_P", .bytes = 2, .write_mask = 0xffff},
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .bytes = 1, .reset = 0xb1},
	{CTRL_REG1, "CTRL_REG1", .bytes = 1, .write_mask = 0x7f},
	{CTRL_REG2, "CTRL_REG2", .bytes = 1, .reset = 0x10, .write_mask = 0xfd,
		       .convert_on_write = BIT(0), .self_clear = BIT(7) | BIT(0)},
	{CTRL_REG3, "CTRL_REG3", .bytes = 1, .write_mask = 0xff},
	{FIFO_CTRL, "FIFO_CTRL", .bytes = 1, .write_mask = 0xff},
	{REF_P, "REF_P", .bytes = 3, .write_mask = 0xffffff},
	{RPDS, "RPDS", .bytes = 2, .write_mask = 0xffff},
	{RES_CONF, "RES_CONF", .bytes = 1, .write_mask = 0x01},
	{INT_SOURCE, "INT_SOURCE", EMUL_SENSOR_REG_RO, .bytes = 1},
	{FIFO_STATUS, "FIFO_STATUS", EMUL_SENSOR_REG_RO, .bytes = 1},
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO, .bytes = 1},
	{PRESS_OUT, "PRESS_OUT", EMUL_SENSOR_REG_RO, .bytes = 3,
	 .read_clears = {.reg = STATUS, .mask = 0x11}},
	{TEMP_OUT, "TEMP_OUT", EMUL_SENSOR_REG_RO, .bytes = 2,
	 .read_clears = {.reg = STATUS, .mask = 0x22}},
	{LPFP_RES, "LPFP_RES", EMUL_SENSOR_REG_RO, .bytes = 1},
};

static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_PRESS, .reg = PRESS_OUT, .is_signed = true, .bits = 24,
	 .lsb = 1.0 / 40960,
	 .min = 26, .max = 126, .ready = {.reg = STATUS, .mask = BIT(0)}, .overrun = BIT(4)},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP_OUT, .is_signed = true, .bits = 16,
	 .lsb = 0.01,
	 .min = -40, .max = 85, .ready = {.reg = STATUS, .mask = BIT(1)}, .overrun = BIT(5)},
};

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	static const uint8_t reset_regs[] = {
		INTERRUPT_CFG, THS_P, CTRL_REG1, CTRL_REG2, CTRL_REG3, FIFO_CTRL, REF_P,
	};

	ARG_UNUSED(old);
	if (reg == CTRL_REG2 && (data->regs[reg] & BIT(2)) != 0U) {
		for (size_t i = 0; i < ARRAY_SIZE(reset_regs); i++) {
			for (size_t j = 0; j < ARRAY_SIZE(registers); j++) {
				if (registers[j].addr == reset_regs[i]) {
					data->regs[reset_regs[i]] = registers[j].reset;
				}
			}
		}
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.byte_addressed = true,
	.increment = {CTRL_REG2, BIT(4)},
	.disabled = {.reg = CTRL_REG1, .mask = 0x70, .value = 0},
	.block_update = {.reg = CTRL_REG1, .mask = BIT(1), .value = BIT(1)},
	.write = write);
