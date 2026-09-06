/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT microchip_mcp9600

#include <zephyr/drivers/emul_sensor_regmap.h>
#include <zephyr/drivers/sensor/mcp9600.h>

/* MCP960X/L0X/RL0X datasheet DS20005426G, section 5, table 5-1 */
static const struct emul_sensor_reg mcp9600_regs[] = {
	{0x00, "TH", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x01, "TDELTA", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x02, "TC", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x03, "RAW_ADC", EMUL_SENSOR_REG_RO, .bytes = 3},
	/* TH Update is normally set, Burst Complete and TH Update are cleared by the host */
	{0x04, "STATUS", .reset = BIT(6)},
	{0x05, "SENSOR_CONFIG"},
	{0x06, "DEVICE_CONFIG"},
	/* Interrupt Clear is forced back to 0 by the device */
	{0x08, "ALERT1_CONFIG", .self_clear = BIT(7)},
	{0x09, "ALERT2_CONFIG", .self_clear = BIT(7)},
	{0x0A, "ALERT3_CONFIG", .self_clear = BIT(7)},
	{0x0B, "ALERT4_CONFIG", .self_clear = BIT(7)},
	{0x0C, "THYST1"},
	{0x0D, "THYST2"},
	{0x0E, "THYST3"},
	{0x0F, "THYST4"},
	{0x10, "TALERT1", .bytes = 2},
	{0x11, "TALERT2", .bytes = 2},
	{0x12, "TALERT3", .bytes = 2},
	{0x13, "TALERT4", .bytes = 2},
	/* Device ID 0x40 (MCP9600), revision 1.0 */
	{0x20, "DEVICE_ID", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x4010},
};

static const struct emul_sensor_channel mcp9600_channels[] = {
	/*
	 * Thermocouple hot-junction temperature, 0.0625 degC/LSB. Range of the type K
	 * thermocouple selected at reset; sets STATUS.TH Update.
	 */
	{SENSOR_CHAN_AMBIENT_TEMP, .reg = 0x00, .is_signed = true, .bits = 16, .lsb = 0.0625,
	 .min = -200.0, .max = 1372.0, .ready = {0x04, BIT(6)}},
	/* Cold-junction temperature, DEVICE_CONFIG bit 7 selects 0.0625 or 0.25 degC/LSB */
	{(enum sensor_channel)SENSOR_CHAN_MCP9600_COLD_JUNCTION_TEMP, .reg = 0x02,
	 .is_signed = true, .min = -40.0, .max = 125.0, .select = {0x06, BIT(7)},
	 .variants = {{.bits = 16, .pos = 0, .lsb = 0.0625}, {.bits = 14, .pos = 2, .lsb = 0.25}},
	 .ready = {0x04, BIT(6)}},
};

EMUL_SENSOR_REGMAP_DEFINE(mcp9600_regs, mcp9600_channels, .big_endian = true,
			  .addr_ignore = GENMASK(7, 6));
