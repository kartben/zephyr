/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT st_lps22hb_press

#include "regmap.h"

/* DocID027083 Rev. 6, sections 7-9: https://www.st.com/resource/en/datasheet/lps22hb.pdf */
enum {
	INTERRUPT_CFG = 0x0b, THS_P = 0x0c, WHO_AM_I = 0x0f,
	CTRL_REG1 = 0x10, CTRL_REG2 = 0x11, CTRL_REG3 = 0x12, FIFO_CTRL = 0x14,
	REF_P = 0x15, RPDS = 0x18, RES_CONF = 0x1a, INT_SOURCE = 0x25,
	FIFO_STATUS = 0x26, STATUS = 0x27, PRESS_OUT = 0x28, TEMP_OUT = 0x2b, LPFP_RES = 0x33,
};

static const struct emul_regmap_register registers[] = {
	[INTERRUPT_CFG] = {.bytes = 1, .write_mask = 0xff},
	[THS_P] = {.bytes = 2, .write_mask = 0xffff},
	[WHO_AM_I] = {.bytes = 1, .reset = 0xb1},
	[CTRL_REG1] = {.bytes = 1, .write_mask = 0x7f},
	[CTRL_REG2] = {.bytes = 1, .reset = 0x10, .write_mask = 0xfd},
	[CTRL_REG3] = {.bytes = 1, .write_mask = 0xff},
	[FIFO_CTRL] = {.bytes = 1, .write_mask = 0xff},
	[REF_P] = {.bytes = 3, .write_mask = 0xffffff},
	[RPDS] = {.bytes = 2, .write_mask = 0xffff},
	[RES_CONF] = {.bytes = 1, .write_mask = 0x01},
	[INT_SOURCE] = {.bytes = 1},
	[FIFO_STATUS] = {.bytes = 1},
	[STATUS] = {.bytes = 1},
	[PRESS_OUT] = {.bytes = 3},
	[TEMP_OUT] = {.bytes = 2},
	[LPFP_RES] = {.bytes = 1},
};

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_PRESS, .reg = PRESS_OUT, .lsb = 1.0 / 40960,
	 .min = 26, .max = 126},
	{.channel = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP_OUT, .lsb = 0.01,
	 .min = -40, .max = 85},
};

static bool sample(const struct emul *target, uint8_t reg, uint32_t value)
{
	struct emul_regmap_data *data = target->data;
	uint32_t ready = reg == PRESS_OUT ? BIT(0) : BIT(1);

	if ((data->values[CTRL_REG1] & 0x70U) == 0U &&
	    (data->values[CTRL_REG2] & BIT(0)) == 0U) {
		return false;
	}
	if ((data->values[CTRL_REG1] & BIT(1)) != 0U &&
	    (data->values[STATUS] & ready) != 0U) {
		return false;
	}
	if ((data->values[STATUS] & ready) != 0U) {
		data->values[STATUS] |= ready << 4;
	}
	data->values[STATUS] |= ready;
	data->values[reg] = value;
	return false;
}

static void read(const struct emul *target, uint8_t reg)
{
	struct emul_regmap_data *data = target->data;

	if (reg == PRESS_OUT + 2U) {
		data->values[STATUS] &= ~0x11U;
	} else if (reg == TEMP_OUT + 1U) {
		data->values[STATUS] &= ~0x22U;
	}
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_regmap_data *data = target->data;
	static const uint8_t reset_regs[] = {
		INTERRUPT_CFG, THS_P, CTRL_REG1, CTRL_REG2, CTRL_REG3, FIFO_CTRL, REF_P,
	};

	ARG_UNUSED(old);
	if (reg == CTRL_REG2) {
		if ((data->values[reg] & BIT(2)) != 0U) {
			for (size_t i = 0; i < ARRAY_SIZE(reset_regs); i++) {
				data->values[reset_regs[i]] = registers[reset_regs[i]].reset;
			}
			return;
		}
		if ((data->values[reg] & BIT(0)) != 0U) {
			emul_regmap_convert(target);
		}
		data->values[reg] &= ~(BIT(7) | BIT(0));
	}
}

static const struct emul_regmap_config config = {
	.registers = registers, .register_count = ARRAY_SIZE(registers),
	.channels = channels, .channel_count = ARRAY_SIZE(channels),
	.byte_addressed = true, .little_endian = true,
	.increment_reg = CTRL_REG2, .increment_mask = BIT(4),
	.read = read, .write = write, .sample = sample,
};

#define DEFINE(inst) EMUL_REGMAP_DT_INST_DEFINE(inst, config, registers, channels);
DT_INST_FOREACH_STATUS_OKAY(DEFINE)
