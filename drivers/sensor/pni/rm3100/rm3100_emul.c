/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT pni_rm3100

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * PNI Sensor "RM3100 & RM2100 Magneto-Inductive Magnetometer User Manual", Doc 1017252 V16.0,
 * section 5 "MagI2C Operation", Table 5-1 "MagI2C Register Map" (register addresses are 7-bit;
 * reading a register ORs 0x80 into the address, which this I2C register model does not use):
 * https://www.pnicorp.com/wp-content/uploads/RM3100-RM2100-Sensor-Suite-User-Manual-V11.0.pdf
 * (the file name is stale; the document served there is the current V16.0 revision).
 *
 * CCX/CCY/CCZ and MX/MY/MZ each occupy a contiguous pair/triple of byte addresses, MSB first
 * (Table 5-2, Table 5-5).
 */
enum {
	POLL = 0x00,
	CMM = 0x01,
	CCX = 0x04,
	CCY = 0x06,
	CCZ = 0x08,
	TMRC = 0x0b,
	MX = 0x24,
	MY = 0x27,
	MZ = 0x2a,
	BIST = 0x33,
	STATUS = 0x34,
	HSHAKE = 0x35,
	REVID = 0x36,
};

static const struct emul_sensor_reg registers[] = {
	/*
	 * PMX/PMY/PMZ (bits 6:4) request a single measurement on that axis; bits 3:0 and bit 7
	 * are reserved, read/write 0 (section 5.3). Writing POLL while Continuous Measurement
	 * Mode is running is NACKed and ignored (section 4.5.1, HSHAKE NACK1); requires_standby
	 * models that using the device disabled condition below. The per-axis selection itself
	 * is not modeled: any of PMX/PMY/PMZ converts all three axes.
	 */
	{POLL, "POLL", .write_mask = GENMASK(6, 4), .convert_on_write = GENMASK(6, 4),
	 .requires_standby = true},
	/*
	 * START (bit 0) enables Continuous Measurement Mode; CMX/CMY/CMZ (bits 6:4) select
	 * which axes it measures and DRDM (bit 2) selects the DRDY condition (section 5.2).
	 * Bits 7, 3 and 1 are reserved. Per-axis CMX/CMY/CMZ selection is not modeled, only
	 * START: turning Continuous Measurement Mode on always converts all three axes
	 * immediately, instead of waiting for the next TMRC tick, which this model does not
	 * implement.
	 */
	{CMM, "CMM", .write_mask = GENMASK(6, 4) | BIT(2) | BIT(0), .convert_on_write = BIT(0)},
	{CCX, "CCX", .bytes = 2, .reset = 0x00c8, .write_mask = 0xffff},
	{CCY, "CCY", .bytes = 2, .reset = 0x00c8, .write_mask = 0xffff},
	{CCZ, "CCZ", .bytes = 2, .reset = 0x00c8, .write_mask = 0xffff},
	/* Bits 7:4 read back fixed at 0x9; TMRC3:0 (bits 3:0) select the update rate (5.2.1). */
	{TMRC, "TMRC", .reset = 0x96, .write_mask = GENMASK(3, 0)},
	/*
	 * DRC1 (HSHAKE bit 1, set by default) clears DRDY when the first byte of a Measurement
	 * Results read is returned; this model clears it once the whole register has been read
	 * instead, which is the closest fit read_clears offers.
	 */
	{MX, "MX", EMUL_SENSOR_REG_RO, .bytes = 3, .read_clears = {STATUS, BIT(7)}},
	{MY, "MY", EMUL_SENSOR_REG_RO, .bytes = 3, .read_clears = {STATUS, BIT(7)}},
	{MZ, "MZ", EMUL_SENSOR_REG_RO, .bytes = 3, .read_clears = {STATUS, BIT(7)}},
	/*
	 * STE (bit 7) starts the built-in self test on the next POLL write; ZOK/YOK/XOK
	 * (bits 6:4) are read-only per-axis pass flags; BW1:0 (bits 3:2) and BP1:0 (bits 1:0)
	 * select timeout and LR period counts (section 5.6.1). STE does not self-clear
	 * (Figure 5-1 clears it with an explicit write). The self test itself is not modeled:
	 * ZOK/YOK/XOK are plain storage, always reading back whatever was last written to them.
	 */
	{BIST, "BIST", .write_mask = 0x8f},
	/* DRDY (bit 7) only; bits 6:0 are indeterminate (section 5.4.1). */
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO},
	/*
	 * DRC0/DRC1 (bits 1:0) select whether DRDY clears on any register write and/or on the
	 * first byte of a Measurement Results read; both are 1 (both mechanisms active) at
	 * reset (section 5.6.2). NACK0/NACK1/NACK2 (bits 4:6) are read-only and not modeled
	 * (always read back as their reset value of 0); bit 3 reads back fixed at 1.
	 */
	{HSHAKE, "HSHAKE", .reset = 0x1b, .write_mask = GENMASK(1, 0)},
	/* Silicon revision; the datasheet leaves the reset value unspecified (section 5.6.3). */
	{REVID, "REVID", EMUL_SENSOR_REG_RO},
};

/*
 * Each axis is a 24-bit two's complement count (range -8388608 to 8388607, section 5.5) over
 * a documented monotonic field range of +/-800 uT (Table 3-1 note 3), converted to Gauss
 * (1 G = 100 uT). Gain is configurable through the Cycle Count registers, but the datasheet
 * gives no formula for arbitrary cycle counts, only three worked examples (Table 3-1); this
 * model uses the reset default of 200 cycle counts, 75 LSB/uT, and does not vary with CCX/
 * CCY/CCZ. Resolution/full-scale .select+.variants does not fit here: the config field is a
 * 16-bit count, not a handful of enumerated settings.
 */
#define GAIN_LSB_PER_UT 75.0
#define MAGN(_chan, _reg)                                                                       \
	{_chan, .reg = _reg, .is_signed = true, .bits = 24,                                       \
	 .lsb = 1.0 / (GAIN_LSB_PER_UT * 100.0), .min = -8.0, .max = 8.0,                         \
	 .ready = {STATUS, BIT(7)}}

static const struct emul_sensor_channel channels[] = {
	MAGN(SENSOR_CHAN_MAGN_X, MX),
	MAGN(SENSOR_CHAN_MAGN_Y, MY),
	MAGN(SENSOR_CHAN_MAGN_Z, MZ),
};

/*
 * Reading CMM and writing TMRC both terminate Continuous Measurement Mode (section 5.2.1
 * note); model both here since only a write can be a convert_on_write bit and this stops
 * conversion rather than causing it.
 */
static void reg_read(const struct emul *target, uint8_t addr)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == CMM) {
		data->regs[CMM] &= ~BIT(0);
	}
}

static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (addr == TMRC) {
		data->regs[CMM] &= ~BIT(0);
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .big_endian = true, .byte_addressed = true,
	.disabled = {.reg = CMM, .mask = BIT(0), .value = 0},
	.read = reg_read, .write = write);
