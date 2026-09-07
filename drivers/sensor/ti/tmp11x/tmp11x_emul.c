/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/drivers/emul_sensor_regmap.h>

/* TMP116 SBOS740A section 7.6; TMP117 SNOSD82D section 7.6:
 * https://www.ti.com/lit/ds/symlink/tmp116.pdf
 * https://www.ti.com/lit/ds/symlink/tmp117.pdf
 */
enum {
	TEMP = 0x00, CONFIG = 0x01, HIGH_LIMIT = 0x02, LOW_LIMIT = 0x03,
	EEPROM_UL = 0x04, EEPROM1 = 0x05, EEPROM2 = 0x06, OFFSET = 0x07,
	EEPROM3 = 0x08, DEVICE_ID = 0x0f,
};

static const struct emul_sensor_reg registers[] = {
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x8000},
	{CONFIG, "CONFIG", .bytes = 2, .reset = 0x0220,
		   .write_mask = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? 0x0ffc : 0x0ffe,
		   .reset_on_write = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? 0 : BIT(1)},
	{HIGH_LIMIT, "HIGH_LIMIT", .bytes = 2, .reset = 0x6000, .write_mask = 0xffff},
	{LOW_LIMIT, "LOW_LIMIT", .bytes = 2, .reset = 0x8000, .write_mask = 0xffff},
	{EEPROM_UL, "EEPROM_UL", .bytes = 2, .write_mask = 0x8000},
	{EEPROM1, "EEPROM1", .bytes = 2, .write_mask = 0xffff},
	{EEPROM2, "EEPROM2", .bytes = 2, .write_mask = 0xffff},
	{OFFSET, "OFFSET", .bytes = 2, .write_mask = 0xffff},
	{EEPROM3, "EEPROM3", .bytes = 2, .write_mask = 0xffff},
	{DEVICE_ID, "DEVICE_ID", EMUL_SENSOR_REG_RO, .bytes = 2,
		      .reset = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? 0x1116 : 0x0117},
};

static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = TEMP, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 128,
	 .min = -55, .max = IS_ENABLED(CONFIG_SENSOR_EMUL_TMP116) ? 125 : 150},
};

static bool sample(const struct emul *target, uint8_t reg, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;
	uint32_t mode = (data->regs[CONFIG] >> 10) & 3U;
	int32_t temperature = (int16_t)value;

	ARG_UNUSED(reg);
	if (mode == 1U) {
		return false;
	}
	if (data->regs[DEVICE_ID] == 0x0117U) {
		temperature += (int16_t)data->regs[OFFSET];
	}
	temperature = CLAMP(temperature, INT16_MIN, INT16_MAX);
	data->regs[TEMP] = (uint16_t)temperature;
	data->regs[CONFIG] |= BIT(13);
	if ((data->regs[CONFIG] & BIT(4)) != 0U) {
		data->regs[CONFIG] &= ~BIT(14);
		if (temperature > (int16_t)data->regs[HIGH_LIMIT]) {
			data->regs[CONFIG] |= BIT(15);
		} else if (temperature < (int16_t)data->regs[LOW_LIMIT]) {
			data->regs[CONFIG] &= ~BIT(15);
		}
	} else {
		if (temperature > (int16_t)data->regs[HIGH_LIMIT]) {
			data->regs[CONFIG] |= BIT(15);
		}
		if (temperature < (int16_t)data->regs[LOW_LIMIT]) {
			data->regs[CONFIG] |= BIT(14);
		}
	}
	if (mode == 3U) {
		data->regs[CONFIG] = (data->regs[CONFIG] & ~0x0c00U) | BIT(10);
	}
	return false;
}

static void read(const struct emul *target, uint8_t reg)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (reg == TEMP || reg == CONFIG) {
		data->regs[CONFIG] &= ~BIT(13);
	}
	if (reg == CONFIG && (data->regs[CONFIG] & BIT(4)) == 0U) {
		data->regs[CONFIG] &= ~(BIT(15) | BIT(14));
	}
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool tmp117 = data->regs[DEVICE_ID] == 0x0117U;

	if (reg >= EEPROM1 && reg <= EEPROM3 && !(tmp117 && reg == OFFSET) &&
	    (data->regs[EEPROM_UL] & BIT(15)) == 0U) {
		data->regs[reg] = old;
	}
	if (reg == CONFIG) {
		if ((data->regs[reg] & 0x0c00U) == 0x0800U) {
			data->regs[reg] &= ~0x0c00U;
		}
		emul_sensor_regmap_convert(target);
	}
}

#define DT_DRV_COMPAT ti_tmp11x
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .fixed_pointer = true,
	.read = read, .write = write, .sample = sample);
