/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ti_ina3221

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * INA3221 SBOS576C (revised September 2025), section 7.6 Register Maps:
 * https://www.ti.com/lit/ds/symlink/ina3221.pdf
 */
enum {
	CONFIGURATION = 0x00,
	CH1_SHUNT_VOLTAGE = 0x01,
	CH1_BUS_VOLTAGE = 0x02,
	CH2_SHUNT_VOLTAGE = 0x03,
	CH2_BUS_VOLTAGE = 0x04,
	CH3_SHUNT_VOLTAGE = 0x05,
	CH3_BUS_VOLTAGE = 0x06,
	CH1_CRITICAL_ALERT_LIMIT = 0x07,
	CH1_WARNING_ALERT_LIMIT = 0x08,
	CH2_CRITICAL_ALERT_LIMIT = 0x09,
	CH2_WARNING_ALERT_LIMIT = 0x0a,
	CH3_CRITICAL_ALERT_LIMIT = 0x0b,
	CH3_WARNING_ALERT_LIMIT = 0x0c,
	SHUNT_VOLTAGE_SUM = 0x0d,
	SHUNT_VOLTAGE_SUM_LIMIT = 0x0e,
	MASK_ENABLE = 0x0f,
	POWER_VALID_UPPER_LIMIT = 0x10,
	POWER_VALID_LOWER_LIMIT = 0x11,
	MANUFACTURER_ID = 0xfe,
	DIE_ID = 0xff,
};

static const struct emul_sensor_reg registers[] = {
	/* RST self-clears and restores every register; a write with MODE1-2 set converts */
	{CONFIGURATION, "Configuration", .reset = 0x7127, .write_mask = 0xffff,
	 .self_clear = BIT(15), .reset_on_write = BIT(15), .convert_on_write = GENMASK(1, 0)},
	{CH1_SHUNT_VOLTAGE, "Channel-1 Shunt Voltage", EMUL_SENSOR_REG_RO},
	{CH1_BUS_VOLTAGE, "Channel-1 Bus Voltage", EMUL_SENSOR_REG_RO},
	{CH2_SHUNT_VOLTAGE, "Channel-2 Shunt Voltage", EMUL_SENSOR_REG_RO},
	{CH2_BUS_VOLTAGE, "Channel-2 Bus Voltage", EMUL_SENSOR_REG_RO},
	{CH3_SHUNT_VOLTAGE, "Channel-3 Shunt Voltage", EMUL_SENSOR_REG_RO},
	{CH3_BUS_VOLTAGE, "Channel-3 Bus Voltage", EMUL_SENSOR_REG_RO},
	{CH1_CRITICAL_ALERT_LIMIT, "Channel-1 Critical Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{CH1_WARNING_ALERT_LIMIT, "Channel-1 Warning Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{CH2_CRITICAL_ALERT_LIMIT, "Channel-2 Critical Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{CH2_WARNING_ALERT_LIMIT, "Channel-2 Warning Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{CH3_CRITICAL_ALERT_LIMIT, "Channel-3 Critical Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{CH3_WARNING_ALERT_LIMIT, "Channel-3 Warning Alert Limit", .reset = 0x7ff8,
	 .write_mask = 0xffff},
	{SHUNT_VOLTAGE_SUM, "Shunt-Voltage Sum", EMUL_SENSOR_REG_RO},
	/* Table 7-33 makes SIGN and bit 0 writable, the field table of Table 7-34 does not */
	{SHUNT_VOLTAGE_SUM_LIMIT, "Shunt-Voltage Sum Limit", .reset = 0x7ffe,
	 .write_mask = 0xffff},
	/* Writing leaves the flag bits alone; reading clears CF1-3, SF, WF1-3 and CVRF */
	{MASK_ENABLE, "Mask/Enable", .reset = 0x0002, .write_mask = 0xfc00,
	 .clear_on_read = GENMASK(9, 3) | BIT(0)},
	/* SIGN is read only on both power-valid limits */
	{POWER_VALID_UPPER_LIMIT, "Power-Valid Upper Limit", .reset = 0x2710,
	 .write_mask = 0x7fff},
	{POWER_VALID_LOWER_LIMIT, "Power-Valid Lower Limit", .reset = 0x2328,
	 .write_mask = 0x7fff},
	{MANUFACTURER_ID, "Manufacturer ID", EMUL_SENSOR_REG_RO, .reset = 0x5449},
	{DIE_ID, "Die ID", EMUL_SENSOR_REG_RO, .reset = 0x3220},
};

/*
 * 13-bit two's complement in bits 15:3, 40uV per LSB reported in millivolts (section 7.6.2.2).
 * MODE1 selects the shunt-voltage measurement, so it is idle in power-down and in bus-only modes.
 */
#define SHUNT(_reg)                                                                                \
	{SENSOR_CHAN_VSHUNT, .reg = _reg, .is_signed = true, .whole_word = true, .bits = 13,       \
	 .pos = 3, .lsb = 0.04, .min = -163.84, .max = 163.8, .ready = {MASK_ENABLE, BIT(0)},      \
	 .disabled = {CONFIGURATION, BIT(0), 0}}

/*
 * 8mV per LSB (section 7.6.2.3); the ADC spans 32.76V but the input is limited to 26V.
 * MODE2 selects the bus-voltage measurement.
 */
#define BUS(_reg)                                                                                  \
	{SENSOR_CHAN_VOLTAGE, .reg = _reg, .is_signed = true, .whole_word = true, .bits = 13,      \
	 .pos = 3, .lsb = 0.008, .min = 0.0, .max = 26.0, .ready = {MASK_ENABLE, BIT(0)},          \
	 .disabled = {CONFIGURATION, BIT(1), 0}}

/*
 * The driver multiplexes the three inputs behind an attribute and reports bus voltage, current
 * and power, so only channel 1's bus voltage is reachable through the sensor API. The shunt
 * registers are modeled as storage; SENSOR_CHAN_VSHUNT has no driver path.
 */
static const struct emul_sensor_channel channels[] = {
	BUS(CH1_BUS_VOLTAGE),
};

/* CH1-3en bypass a channel entirely; SCC1-3 select the channels filling the Shunt-Voltage Sum */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;
	uint8_t channel = (addr - CH1_SHUNT_VOLTAGE) / 2U;
	int32_t sum = 0;

	if ((data->regs[CONFIGURATION] & BIT(14 - channel)) == 0U) {
		return false;
	}

	/* Only shunt conversions, at the odd addresses, feed the sum */
	if ((addr % 2U) == 0U) {
		return true;
	}

	for (uint8_t i = 0; i < 3U; i++) {
		uint8_t reg = CH1_SHUNT_VOLTAGE + (2U * i);
		uint32_t raw = (reg == addr) ? value : data->regs[reg];

		if ((data->regs[MASK_ENABLE] & BIT(14 - i)) != 0U) {
			sum += sign_extend(raw >> 3, 12);
		}
	}

	/* Both registers count in 40uV steps; the sum occupies bits 15:1 */
	data->regs[SHUNT_VOLTAGE_SUM] = ((uint32_t)sum << 1) & GENMASK(15, 1);

	return true;
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .big_endian = true,
	.fixed_pointer = true,
	.disabled = {.reg = CONFIGURATION, .mask = GENMASK(1, 0), .value = 0},
	.sample = sample);
