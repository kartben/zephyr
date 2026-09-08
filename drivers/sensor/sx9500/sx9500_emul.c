/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT semtech_sx9500

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Semtech SX9500 "Ultra Low Power, Capacitive Four (4) - Channel Proximity/Button" datasheet,
 * Revision 1.6, June 27, 2012, section 6.3 (I2C Interface, Register Overview, Table 16):
 * https://datasheet.octopart.com/SX9500IULTRT-Semtech-datasheet-12513387.pdf
 */
enum {
	IRQSTAT = 0x00,
	TCHCMPSTAT = 0x01,
	IRQ_ENABLE = 0x03,
	CPS_CTRL0 = 0x06,
	CPS_CTRL1 = 0x07,
	CPS_CTRL2 = 0x08,
	CPS_CTRL3 = 0x09,
	CPS_CTRL4 = 0x0a,
	CPS_CTRL5 = 0x0b,
	CPS_CTRL6 = 0x0c,
	CPS_CTRL7 = 0x0d,
	CPS_CTRL8 = 0x0e,
	CPSRD = 0x20,
	USE_MSB = 0x21,
	USE_LSB = 0x22,
	AVG_MSB = 0x23,
	AVG_LSB = 0x24,
	DIFF_MSB = 0x25,
	DIFF_LSB = 0x26,
	OFF_MSB = 0x27,
	OFF_LSB = 0x28,
	I2C_SOFT_RESET = 0x7f,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * RESETIRQ, TCHIRQ, RLSIRQ, COMPDONE and CONVIRQ are interrupt-status bits, cleared when
	 * IRQStat is read (sections 3.6.1, 3.6.2). Bit 4 (COMPDONE) is also writable: writing a
	 * one requests a compensation cycle on all channels, self-clearing once issued.
	 */
	{IRQSTAT, "IRQStat", .reset = 0x80, .write_mask = BIT(4), .self_clear = BIT(4),
	 .clear_on_read = GENMASK(7, 3)},
	{TCHCMPSTAT, "TchCmpStat", EMUL_SENSOR_REG_RO, .reset = 0x0f},
	{IRQ_ENABLE, "IRQ_Enable", .write_mask = GENMASK(6, 3)},
	{CPS_CTRL0, "CPS_CTRL0", .reset = 0x0f, .write_mask = GENMASK(6, 0)},
	{CPS_CTRL1, "CPS_CTRL1", .reset = 0x40, .write_mask = GENMASK(7, 6) | GENMASK(1, 0)},
	{CPS_CTRL2, "CPS_CTRL2", .reset = 0x08, .write_mask = GENMASK(6, 0)},
	{CPS_CTRL3, "CPS_CTRL3", .reset = 0x40,
	 .write_mask = BIT(6) | GENMASK(5, 4) | GENMASK(1, 0)},
	{CPS_CTRL4, "CPS_CTRL4", .write_mask = 0xff},
	{CPS_CTRL5, "CPS_CTRL5", .write_mask = 0xff},
	{CPS_CTRL6, "CPS_CTRL6", .write_mask = GENMASK(4, 0)},
	{CPS_CTRL7, "CPS_CTRL7", .write_mask = 0xff},
	{CPS_CTRL8, "CPS_CTRL8", .write_mask = 0xff},
	{CPSRD, "CPSRD", .write_mask = GENMASK(1, 0)},
	{USE_MSB, "UseMSB", EMUL_SENSOR_REG_RO},
	{USE_LSB, "UseLSB", EMUL_SENSOR_REG_RO},
	{AVG_MSB, "AvgMSB", EMUL_SENSOR_REG_RO},
	{AVG_LSB, "AvgLSB", EMUL_SENSOR_REG_RO},
	{DIFF_MSB, "DiffMSB", EMUL_SENSOR_REG_RO},
	{DIFF_LSB, "DiffLSB", EMUL_SENSOR_REG_RO},
	{OFF_MSB, "OffMSB", .write_mask = 0xff},
	{OFF_LSB, "OffLSB", .write_mask = 0xff},
	/* Write-only; the host writes 0xDE to request a full software reset (section 3.5.3). */
	{I2C_SOFT_RESET, "I2CSoftReset", .write_mask = 0xff, .self_clear = 0xff},
};

/*
 * TchCmpStat bit 4 (TCHSTAT0) is a live boolean: touch/proximity detected on CS0 or not,
 * matching SENSOR_CHAN_PROX's adimensional "1 means close" definition exactly. CS1-CS3
 * (bits 5-7) report the same kind of value for the other three capacitive inputs, but
 * sensor_channel.h has no per-instance proximity channel to expose them as, so only CS0 is
 * modeled; CS1-CS3 are left out. UseMSB/LSB, AvgMSB/LSB and DiffMSB/LSB (selected through
 * CPSRD) report the raw capacitive signal, its running average and their difference in
 * device-internal ADC counts with no datasheet-defined physical unit, so none of them map to
 * a sensor_channel either.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_PROX, .reg = TCHCMPSTAT, .bits = 1, .pos = 4, .lsb = 1.0,
	 .min = 0, .max = 1,
	 .disabled = {.reg = CPS_CTRL0, .mask = BIT(0), .value = 0},
	 /* TCHIRQ (object approached) and RLSIRQ (object left) both signal a new sample. */
	 .ready = {IRQSTAT, BIT(6) | BIT(5)}},
};

/*
 * The device converts continuously while CS0 is enabled, so injecting a new proximity state
 * updates TCHSTAT0 on its own. But convert_on_write only fires on a bit written as one, and
 * there is no such bit here; when the host re-enables CS0 (CPS_EN bit 0 going 0 -> 1) after
 * disabling it, the retained input must be converted explicitly. I2CSoftReset also needs an
 * exact byte match that write_mask/self_clear alone cannot express.
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == CPS_CTRL0 && (old & BIT(0)) == 0U && (data->regs[CPS_CTRL0] & BIT(0)) != 0U) {
		emul_sensor_regmap_convert(target);
	}
	if (addr == I2C_SOFT_RESET && data->regs[I2C_SOFT_RESET] == 0xdeU) {
		emul_sensor_regmap_reset(target);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .write = write);
