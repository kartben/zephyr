/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT allegro_als31300

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * ALS31300-DS, Rev. 14 (MCO-0000228), MEMORY MAP (Table 1), EEPROM (Tables 2 to 4) and PRIMARY
 * REGISTERS (Tables 5 to 7):
 * https://www.allegromicro.com/-/media/files/datasheets/als31300-datasheet.pdf
 */
enum {
	EEPROM_02 = 0x02,
	EEPROM_03 = 0x03,
	EEPROM_0D = 0x0D,
	EEPROM_0E = 0x0E,
	EEPROM_0F = 0x0F,
	VOLATILE_27 = 0x27,
	VOLATILE_28 = 0x28,
	VOLATILE_29 = 0x29,
	CUSTOMER_ACCESS = 0x35,
};

/* Table 16 */
#define CUSTOMER_ACCESS_CODE 0x2C413534

static const struct emul_sensor_reg registers[] = {
	/* I2C Threshold and the three Channel Enable bits are the only fields set at reset. */
	{EEPROM_02, "EEPROM 0x02", .reset = 0x3C0, .write_mask = 0x00FFFFFF},
	{EEPROM_03, "EEPROM 0x03", .write_mask = 0x01FFFFFF},
	{EEPROM_0D, "EEPROM 0x0D", .write_mask = 0x03FFFFFF},
	{EEPROM_0E, "EEPROM 0x0E", .write_mask = 0x03FFFFFF},
	{EEPROM_0F, "EEPROM 0x0F", .write_mask = 0x03FFFFFF},
	{VOLATILE_27, "Volatile 0x27", .write_mask = 0x7F},
	/* Interrupt is write 1 to clear, New Data clears on a read of 0x28. */
	{VOLATILE_28, "Volatile 0x28", .write_mask = BIT(6), .self_clear = BIT(6),
		       .clear_on_read = BIT(7)},
	{VOLATILE_29, "Volatile 0x29", EMUL_SENSOR_REG_RO},
	{CUSTOMER_ACCESS, "Customer Access Code"},
};

/*
 * Selection guide: 4 LSB/G over +/-500 G for the -500 part, 2 LSB/G over +/-1000 G for -1000,
 * 1 LSB/G over +/-2000 G for -2000, and 1 LSB/G on X and Y with 0.25 LSB/G over +/-8000 G on Z
 * for -JOY. The trim is factory programmed and no register reports it.
 */
#define SENS_LSB_PER_G 4.0
#define RANGE_G 500.0

/*
 * Table 8: the 12-bit two's complement sample is split, its 8 MSBs in 0x28 and its 4 LSBs in
 * 0x29, so one modeled count carries 16 of the 12-bit codes. New Data flags X, Y and Z only.
 */
#define MAGN(_chan, _pos, _enable)                                                              \
	{_chan, .reg = VOLATILE_28, .is_signed = true, .bits = 8, .pos = _pos,                  \
	 .lsb = 16.0 / SENS_LSB_PER_G, .min = -RANGE_G, .max = RANGE_G,                         \
	 .ready = {VOLATILE_28, BIT(7)},                                                        \
	 .disabled = {.reg = EEPROM_02, .mask = BIT(_enable), .value = 0}}

static const struct emul_sensor_channel channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, 24, 6),
	MAGN(SENSOR_CHAN_MAGN_Y, 16, 7),
	MAGN(SENSOR_CHAN_MAGN_Z, 8, 8),
	/* temp = 302 * (code - 1708) / 4096, the 6 modeled MSBs carrying 64 codes each. */
	{SENSOR_CHAN_DIE_TEMP, .reg = VOLATILE_28, .bits = 6, .lsb = 302.0 * 64 / 4096,
	 .offset = -302.0 * 1708 / 4096, .min = -40.0, .max = 85.0},
};

static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == CUSTOMER_ACCESS) {
		/* Access mode cannot be left again without a power cycle. */
		if (old == CUSTOMER_ACCESS_CODE) {
			data->regs[addr] = old;
		}
	} else if (addr != VOLATILE_27 && data->regs[CUSTOMER_ACCESS] != CUSTOMER_ACCESS_CODE) {
		/* Only the sleep register is writable outside customer access mode. */
		data->regs[addr] = old;
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 4, .big_endian = true,
	.disabled = {.reg = VOLATILE_27, .mask = GENMASK(1, 0), .value = 1},
	.write = write);
