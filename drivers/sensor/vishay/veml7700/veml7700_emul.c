/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT vishay_veml7700

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Document Number 84286, Rev. 1.8, 28-Nov-2024, command register format and tables 1 to 8:
 * https://www.vishay.com/docs/84286/veml7700.pdf
 */
enum {
	ALS_CONF_0 = 0x00,
	ALS_WH = 0x01,
	ALS_WL = 0x02,
	PSM = 0x03,
	ALS = 0x04,
	WHITE = 0x05,
	ALS_INT = 0x06,
	ID = 0x07,
};

/* ALS_GAIN 12:11, ALS_IT 9:6, ALS_PERS 5:4, ALS_INT_EN 1, ALS_SD 0; the rest is set to 0 */
#define ALS_CONF_0_WRITE_MASK (GENMASK(12, 11) | GENMASK(9, 6) | GENMASK(5, 4) | GENMASK(1, 0))

static const struct emul_sensor_reg registers[] = {
	/* Default value 0x0001, the device is shut down */
	{ALS_CONF_0, "ALS_CONF_0", .reset = 0x0001, .write_mask = ALS_CONF_0_WRITE_MASK},
	{ALS_WH, "ALS_WH", .write_mask = 0xffff},
	{ALS_WL, "ALS_WL", .write_mask = 0xffff},
	/* PSM 2:1 and PSM_EN 0; the address list on page 6 still calls 03h reserved */
	{PSM, "PSM", .write_mask = GENMASK(2, 0)},
	{ALS, "ALS", EMUL_SENSOR_REG_RO},
	{WHITE, "WHITE", EMUL_SENSOR_REG_RO},
	{ALS_INT, "ALS_INT", EMUL_SENSOR_REG_RO},
	/* Slave address option code 0xc4 for bus address 0x20, device ID code 0x81 */
	{ID, "ID", EMUL_SENSOR_REG_RO, .reset = 0xc481},
};

/*
 * 0.0036 lx/count at ALS_GAIN = x2 and ALS_IT = 800 ms; the resolution doubles each time the
 * integration time halves, so the 100 ms reset value is eight times that. The resolution scales
 * with the inverse of the gain, so ALS_GAIN 00 = x1, 01 = x2, 10 = x1/8 and 11 = x1/4 give 2, 1,
 * 16 and 8 times that value.
 */
#define ALS_LSB_X2_100MS 0.0288

#define ALS_CHANNEL(_chan, _reg)                                                                   \
	{_chan, .reg = _reg, .bits = 16, .select = {ALS_CONF_0, GENMASK(12, 11)},                  \
	 .variants = {{.lsb = ALS_LSB_X2_100MS * 2},                                               \
		      {.lsb = ALS_LSB_X2_100MS},                                                   \
		      {.lsb = ALS_LSB_X2_100MS * 16},                                              \
		      {.lsb = ALS_LSB_X2_100MS * 8}}}

/*
 * The WHITE output has no lx/count in the datasheet and no matching enum sensor_channel value,
 * so it is left as register storage rather than mapped onto an unrelated channel.
 */
static const struct emul_sensor_channel channels[] = {
	ALS_CHANNEL(SENSOR_CHAN_LIGHT, ALS),
};

/*
 * The part converts continuously while ALS_SD is clear, so clearing it starts a conversion of
 * the retained input. convert_on_write only fires on a bit written as one, so leaving shutdown
 * is handled here.
 */
static void write_reg(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == ALS_CONF_0 && (old & BIT(0)) != 0U && (data->regs[ALS_CONF_0] & BIT(0)) == 0U) {
		emul_sensor_regmap_convert(target);
	}
}

/* Each command code holds one 16 bit word sent low byte first and is resent for every access */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2, .fixed_pointer = true,
	.disabled = {.reg = ALS_CONF_0, .mask = BIT(0), .value = BIT(0)}, .write = write_reg);
