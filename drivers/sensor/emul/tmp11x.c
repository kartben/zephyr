/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "regmap.h"

/* TMP116 SBOS740A section 7.6; TMP117 SNOSD82D section 7.6:
 * https://www.ti.com/lit/ds/symlink/tmp116.pdf
 * https://www.ti.com/lit/ds/symlink/tmp117.pdf
 */
enum {
	TEMP = 0x00, CONFIG = 0x01, HIGH_LIMIT = 0x02, LOW_LIMIT = 0x03,
	EEPROM_UL = 0x04, EEPROM1 = 0x05, EEPROM2 = 0x06, OFFSET = 0x07,
	EEPROM3 = 0x08, DEVICE_ID = 0x0f,
};

#define REGISTERS(id, mask) {                                                                  \
	[TEMP] = {.bytes = 2, .reset = 0x8000},                                                \
	[CONFIG] = {.bytes = 2, .reset = 0x0220, .write_mask = mask},                          \
	[HIGH_LIMIT] = {.bytes = 2, .reset = 0x6000, .write_mask = 0xffff},                    \
	[LOW_LIMIT] = {.bytes = 2, .reset = 0x8000, .write_mask = 0xffff},                     \
	[EEPROM_UL] = {.bytes = 2, .write_mask = 0x8000},                                      \
	[EEPROM1] = {.bytes = 2, .write_mask = 0xffff},                                        \
	[EEPROM2] = {.bytes = 2, .write_mask = 0xffff},                                        \
	[OFFSET] = {.bytes = 2, .write_mask = 0xffff},                                         \
	[EEPROM3] = {.bytes = 2, .write_mask = 0xffff},                                        \
	[DEVICE_ID] = {.bytes = 2, .reset = id},                                               \
}

static const struct emul_regmap_register tmp116_registers[] = REGISTERS(0x1116, 0x0ffc);
static const struct emul_regmap_register tmp117_registers[] = REGISTERS(0x0117, 0x0ffe);

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .lsb = 1.0 / 128,
	 .min = -55, .max = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? 125 : 150},
};

static bool sample(const struct emul *target, uint8_t reg, uint32_t value)
{
	struct emul_regmap_data *data = target->data;
	uint32_t mode = (data->values[CONFIG] >> 10) & 3U;
	int32_t temperature = (int16_t)value;

	ARG_UNUSED(reg);
	if (mode == 1U) {
		return false;
	}
	if (data->values[DEVICE_ID] == 0x0117U) {
		temperature += (int16_t)data->values[OFFSET];
	}
	temperature = CLAMP(temperature, INT16_MIN, INT16_MAX);
	data->values[TEMP] = (uint16_t)temperature;
	data->values[CONFIG] |= BIT(13);
	if ((data->values[CONFIG] & BIT(4)) != 0U) {
		data->values[CONFIG] &= ~BIT(14);
		if (temperature > (int16_t)data->values[HIGH_LIMIT]) {
			data->values[CONFIG] |= BIT(15);
		} else if (temperature < (int16_t)data->values[LOW_LIMIT]) {
			data->values[CONFIG] &= ~BIT(15);
		}
	} else {
		if (temperature > (int16_t)data->values[HIGH_LIMIT]) {
			data->values[CONFIG] |= BIT(15);
		}
		if (temperature < (int16_t)data->values[LOW_LIMIT]) {
			data->values[CONFIG] |= BIT(14);
		}
	}
	if (mode == 3U) {
		data->values[CONFIG] = (data->values[CONFIG] & ~0x0c00U) | BIT(10);
	}
	return false;
}

static void read(const struct emul *target, uint8_t reg)
{
	struct emul_regmap_data *data = target->data;

	if (reg == TEMP || reg == CONFIG) {
		data->values[CONFIG] &= ~BIT(13);
	}
	if (reg == CONFIG && (data->values[CONFIG] & BIT(4)) == 0U) {
		data->values[CONFIG] &= ~(BIT(15) | BIT(14));
	}
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_regmap_data *data = target->data;
	bool tmp117 = data->values[DEVICE_ID] == 0x0117U;

	if (reg >= EEPROM1 && reg <= EEPROM3 && !(tmp117 && reg == OFFSET) &&
	    (data->values[EEPROM_UL] & BIT(15)) == 0U) {
		data->values[reg] = old;
	}
	if (reg == CONFIG) {
		if (tmp117 && (data->values[reg] & BIT(1)) != 0U) {
			emul_regmap_reset(target);
			return;
		}
		if ((data->values[reg] & 0x0c00U) == 0x0800U) {
			data->values[reg] &= ~0x0c00U;
		}
		emul_regmap_convert(target);
	}
}

#define DT_DRV_COMPAT ti_tmp11x
static const struct emul_regmap_config config = {
	.registers = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? tmp116_registers : tmp117_registers,
	.register_count = ARRAY_SIZE(tmp117_registers),
	.channels = channels, .channel_count = ARRAY_SIZE(channels),
	.read = read, .write = write, .sample = sample,
};

#define DEFINE(inst) EMUL_REGMAP_DT_INST_DEFINE(inst, config, tmp117_registers, channels);
DT_INST_FOREACH_STATUS_OKAY(DEFINE)
