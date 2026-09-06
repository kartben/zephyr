/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT honeywell_hmc5883l

#include <zephyr/drivers/emul_sensor_regmap.h>

/* HMC5883L 3-Axis Digital Compass IC datasheet, Form 900405 Rev E, Registers, tables 2 to 20 */
static const struct emul_sensor_reg hmc5883l_regs[] = {
	{0x00, "CRA", .reset = 0x10},
	{0x01, "CRB", .reset = 0x20},
	{0x02, "MR", .reset = 0x01},
	{0x03, "DXRA", EMUL_SENSOR_REG_RO},
	{0x04, "DXRB", EMUL_SENSOR_REG_RO},
	{0x05, "DZRA", EMUL_SENSOR_REG_RO},
	{0x06, "DZRB", EMUL_SENSOR_REG_RO},
	{0x07, "DYRA", EMUL_SENSOR_REG_RO},
	{0x08, "DYRB", EMUL_SENSOR_REG_RO},
	{0x09, "SR", EMUL_SENSOR_REG_RO},
	/* ASCII "H43" */
	{0x0A, "IRA", EMUL_SENSOR_REG_RO, .reset = 0x48},
	{0x0B, "IRB", EMUL_SENSOR_REG_RO, .reset = 0x34},
	{0x0C, "IRC", EMUL_SENSOR_REG_RO, .reset = 0x33},
};

/*
 * 16-bit two's complement output from a 12-bit ADC (0xF800 to 0x07FF). CRB.GN selects the gain:
 * 1370, 1090, 820, 660, 440, 390, 330, 230 LSb/gauss. +-1.3 gauss fits the ADC at every gain.
 * Sets SR.RDY.
 */
#define MAGN(_chan, _reg)                                                                          \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16, .min = -1.3, .max = 1.3,               \
	 .select = {0x01, GENMASK(7, 5)},                                                          \
	 .variants = {{.lsb = 1.0 / 1370}, {.lsb = 1.0 / 1090}, {.lsb = 1.0 / 820},                \
		      {.lsb = 1.0 / 660}, {.lsb = 1.0 / 440}, {.lsb = 1.0 / 390},                  \
		      {.lsb = 1.0 / 330}, {.lsb = 1.0 / 230}},                                     \
	 .ready = {0x09, BIT(0)}}

static const struct emul_sensor_channel hmc5883l_channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, 0x03),
	MAGN(SENSOR_CHAN_MAGN_Z, 0x05),
	MAGN(SENSOR_CHAN_MAGN_Y, 0x07),
};

EMUL_SENSOR_REGMAP_DEFINE(hmc5883l_regs, hmc5883l_channels, .big_endian = true);
