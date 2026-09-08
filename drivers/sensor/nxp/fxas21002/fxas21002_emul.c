/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT nxp_fxas21002

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * FXAS21002C, Rev. 2.1, 5/2015, section 6 Register Descriptions (Table 13 register address
 * map, tables 14-53 per-register fields): https://www.nxp.com/docs/en/data-sheet/FXAS21002.pdf
 *
 * FS_DOUBLE (CTRL_REG3 bit 0) doubles the dynamic range selected by CTRL_REG0[FS], but the
 * framework's .select keys a single register's bits; FS and FS_DOUBLE live in two different
 * registers, so FS_DOUBLE is stored but not modeled (Table 35's nominal, non-doubled
 * sensitivities apply always). FIFO storage (F_SETUP/F_STATUS/F_EVENT), the rate-threshold
 * comparator (RT_CFG/RT_SRC/RT_THS/RT_COUNT), INT_SRC_FLAG aggregation, the high/low-pass
 * filters, self-test and SPI 3-/4-wire selection are register storage only; their derived
 * behavior is not simulated. STATUS (0x00) is documented as a live alias of DR_STATUS or
 * F_STATUS depending on FIFO mode; the framework has no verified read-time computed-value
 * hook, so it is modeled as its own read-only register that always reads its reset value
 * instead of being kept in sync -- read DR_STATUS (0x07) directly.
 */
enum {
	STATUS = 0x00,
	OUT_X_MSB = 0x01,
	OUT_X_LSB = 0x02,
	OUT_Y_MSB = 0x03,
	OUT_Y_LSB = 0x04,
	OUT_Z_MSB = 0x05,
	OUT_Z_LSB = 0x06,
	DR_STATUS = 0x07,
	F_STATUS = 0x08,
	F_SETUP = 0x09,
	F_EVENT = 0x0a,
	INT_SRC_FLAG = 0x0b,
	WHO_AM_I = 0x0c,
	CTRL_REG0 = 0x0d,
	RT_CFG = 0x0e,
	RT_SRC = 0x0f,
	RT_THS = 0x10,
	RT_COUNT = 0x11,
	TEMP = 0x12,
	CTRL_REG1 = 0x13,
	CTRL_REG2 = 0x14,
	CTRL_REG3 = 0x15,
};

static const struct emul_sensor_reg registers[] = {
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO},
	{OUT_X_MSB, "OUT_X_MSB", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = DR_STATUS, .mask = BIT(0) | BIT(4)}},
	{OUT_X_LSB, "OUT_X_LSB", EMUL_SENSOR_REG_RO},
	{OUT_Y_MSB, "OUT_Y_MSB", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = DR_STATUS, .mask = BIT(1) | BIT(5)}},
	{OUT_Y_LSB, "OUT_Y_LSB", EMUL_SENSOR_REG_RO},
	/*
	 * ZYXOW/ZYXDR (bits 7 and 3) clear only once all three MSBs have been read (Table 21);
	 * the register model has no per-transaction burst state, so they are cleared here, on
	 * the last register of the documented six-byte burst (section 6.2 NOTE).
	 */
	{OUT_Z_MSB, "OUT_Z_MSB", EMUL_SENSOR_REG_RO,
	 .read_clears = {.reg = DR_STATUS, .mask = BIT(2) | BIT(3) | BIT(6) | BIT(7)}},
	{OUT_Z_LSB, "OUT_Z_LSB", EMUL_SENSOR_REG_RO},
	{DR_STATUS, "DR_STATUS", EMUL_SENSOR_REG_RO},
	{F_STATUS, "F_STATUS", EMUL_SENSOR_REG_RO},
	{F_SETUP, "F_SETUP", .write_mask = 0xff},
	{F_EVENT, "F_EVENT", EMUL_SENSOR_REG_RO},
	{INT_SRC_FLAG, "INT_SRC_FLAG", EMUL_SENSOR_REG_RO},
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0xd7},
	{CTRL_REG0, "CTRL_REG0", .write_mask = 0xff},
	/* Bits 7:4 are unimplemented and always read 0 (Table 36). */
	{RT_CFG, "RT_CFG", .write_mask = 0x0f},
	{RT_SRC, "RT_SRC", EMUL_SENSOR_REG_RO},
	{RT_THS, "RT_THS", .write_mask = 0xff},
	{RT_COUNT, "RT_COUNT", .reset = 0x01, .write_mask = 0xff},
	{TEMP, "TEMP", EMUL_SENSOR_REG_RO},
	/* RST (bit 6) self-clears and restores reset values (section 6.15, Table 46). */
	{CTRL_REG1, "CTRL_REG1", .write_mask = 0x7f, .self_clear = BIT(6),
	 .reset_on_write = BIT(6)},
	{CTRL_REG2, "CTRL_REG2", .write_mask = 0xff},
	/* Bits 7:4 and 1 are unimplemented (Table 52); only WRAPTOONE, EXTCTRLEN and
	 * FS_DOUBLE are writable.
	 */
	{CTRL_REG3, "CTRL_REG3", .write_mask = BIT(3) | BIT(2) | BIT(0)},
};

/*
 * 16-bit two's complement rate data, MSB at the lower address (section 5, Table 14). Sensitivity
 * is set by CTRL_REG0[FS] (Table 35): 62.5/31.25/15.625/7.8125 mdps/LSB for +-2000/1000/500/250
 * dps, the array order matching FS[1:0] read as an unsigned 2-bit value directly.
 */
#define DPS_TO_RAD_S (3.14159265358979323846 / 180.0)
#define DPS(x) ((x) * DPS_TO_RAD_S)

static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_GYRO_X, .reg = OUT_X_MSB, .is_signed = true, .whole_word = true,
	 .bits = 16,
	 .select = {.reg = CTRL_REG0, .mask = GENMASK(1, 0)},
	 .variants = {{.lsb = DPS(0.0625), .min = -DPS(2000), .max = DPS(2000)},
		      {.lsb = DPS(0.03125), .min = -DPS(1000), .max = DPS(1000)},
		      {.lsb = DPS(0.015625), .min = -DPS(500), .max = DPS(500)},
		      {.lsb = DPS(0.0078125), .min = -DPS(250), .max = DPS(250)}},
	 .ready = {.reg = DR_STATUS, .mask = BIT(0) | BIT(3)},
	 .overrun = BIT(4) | BIT(7)},
	{.chan = SENSOR_CHAN_GYRO_Y, .reg = OUT_Y_MSB, .is_signed = true, .whole_word = true,
	 .bits = 16,
	 .select = {.reg = CTRL_REG0, .mask = GENMASK(1, 0)},
	 .variants = {{.lsb = DPS(0.0625), .min = -DPS(2000), .max = DPS(2000)},
		      {.lsb = DPS(0.03125), .min = -DPS(1000), .max = DPS(1000)},
		      {.lsb = DPS(0.015625), .min = -DPS(500), .max = DPS(500)},
		      {.lsb = DPS(0.0078125), .min = -DPS(250), .max = DPS(250)}},
	 .ready = {.reg = DR_STATUS, .mask = BIT(1) | BIT(3)},
	 .overrun = BIT(5) | BIT(7)},
	{.chan = SENSOR_CHAN_GYRO_Z, .reg = OUT_Z_MSB, .is_signed = true, .whole_word = true,
	 .bits = 16,
	 .select = {.reg = CTRL_REG0, .mask = GENMASK(1, 0)},
	 .variants = {{.lsb = DPS(0.0625), .min = -DPS(2000), .max = DPS(2000)},
		      {.lsb = DPS(0.03125), .min = -DPS(1000), .max = DPS(1000)},
		      {.lsb = DPS(0.015625), .min = -DPS(500), .max = DPS(500)},
		      {.lsb = DPS(0.0078125), .min = -DPS(250), .max = DPS(250)}},
	 .ready = {.reg = DR_STATUS, .mask = BIT(2) | BIT(3)},
	 .overrun = BIT(6) | BIT(7)},
	/* 8-bit two's complement, 1 degC/LSB, full encoding range -128 to +127 degC
	 * (section 6.14).
	 */
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TEMP, .is_signed = true, .bits = 8, .lsb = 1.0,
	 .min = -128, .max = 127},
};

/*
 * Active mode (ACTIVE=1) measures continuously; Ready/Standby (ACTIVE=0) retain the last
 * input without converting (section 4, Figure 18). Setting ACTIVE does not go through
 * convert_on_write (that only fires when the written bit itself is one on every write, not on
 * a 0->1 transition read back from the register), so continuous conversion on waking is
 * restarted explicitly here. DR_STATUS, F_STATUS, F_EVENT and RT_SRC are also documented to
 * reset on every Standby/Ready -> Active transition (sections 6.3, 6.4, 6.6, 6.11).
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	bool was_active, now_active;

	if (addr != CTRL_REG1) {
		return;
	}

	was_active = (old & BIT(1)) != 0U;
	now_active = (data->regs[CTRL_REG1] & BIT(1)) != 0U;

	if (!was_active && now_active) {
		data->regs[DR_STATUS] = 0U;
		data->regs[F_STATUS] = 0U;
		data->regs[F_EVENT] = 0U;
		data->regs[RT_SRC] = 0U;
		emul_sensor_regmap_convert(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true,
	.disabled = {.reg = CTRL_REG1, .mask = BIT(1), .value = 0},
	.write = write);
