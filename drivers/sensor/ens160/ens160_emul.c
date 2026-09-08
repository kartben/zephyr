/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT sciosense_ens160

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * ENS160 Datasheet Version 1.3, Doc No. SC-001224-DS-9, Release Date 2023-03-29, section 16
 * "Registers" (Table 16 Register overview, Tables 17-37 detailed register description):
 * https://www.sciosense.com/wp-content/uploads/2023/12/ENS160-Datasheet.pdf
 *
 * Addresses are literal byte offsets: a 2-byte register occupies its address and the next one,
 * little endian (section 16.1). Table 16 also lists the gaps below as explicit "Reserved" rows
 * with either no defined access ("-") or, for 0x2A, "Read"; they are modeled read-only at their
 * undocumented reset value of 0x00. The addresses Table 16 omits entirely (0x02-0x0F, 0x39-0x3F)
 * are not part of the map and correctly fail with -EIO.
 */
enum {
	PART_ID = 0x00,		/* 2 bytes: 0x60, 0x01 (section 16.2.1) */
	OPMODE = 0x10,
	CONFIG_REG = 0x11,
	COMMAND = 0x12,
	TEMP_IN = 0x13,		/* 2 bytes */
	RH_IN = 0x15,		/* 2 bytes */
	DEVICE_STATUS = 0x20,
	DATA_AQI = 0x21,
	DATA_TVOC = 0x22,	/* 2 bytes; DATA_ETOH (section 16.2.11) is the same register */
	DATA_ECO2 = 0x24,	/* 2 bytes */
	DATA_T = 0x30,		/* 2 bytes */
	DATA_RH = 0x32,		/* 2 bytes */
	DATA_MISR = 0x38,
	GPR_WRITE0 = 0x40,
	GPR_READ0 = 0x48,
};

/* OPMODE values (Table 18, Figure 13). */
enum {
	OPMODE_DEEP_SLEEP = 0x00,
	OPMODE_IDLE = 0x01,
	OPMODE_STANDARD = 0x02,
	OPMODE_RESET = 0xf0,
};

/* DEVICE_STATUS.NEWDAT (bit 1): new DATA_x sample, cleared on the first DATA_x read. */
#define DEVICE_STATUS_NEWDAT BIT(1)
/* DEVICE_STATUS.NEWGPR (bit 0): new GPR_READx data, cleared on the first GPR_READx read. */
#define DEVICE_STATUS_NEWGPR BIT(0)

static const struct emul_sensor_reg registers[] = {
	{PART_ID, "PART_ID", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x0160},
	/* Any write terminates the running OPMODE and starts the new one (section 15). */
	{OPMODE, "OPMODE", .reset = OPMODE_IDLE, .reset_on_write = OPMODE_RESET},
	/* INTPOL, INT_CFG, INTGPR, INTDAT, INTEN; bits 7, 4 and 2 are reserved. */
	{CONFIG_REG, "CONFIG", .write_mask = 0x6b},
	{COMMAND, "COMMAND", .write_mask = 0xff},
	{TEMP_IN, "TEMP_IN", .bytes = 2},
	{RH_IN, "RH_IN", .bytes = 2},
	{0x17, "Reserved", EMUL_SENSOR_REG_RO},
	{0x18, "Reserved", EMUL_SENSOR_REG_RO},
	{0x19, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1a, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1b, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x1f, "Reserved", EMUL_SENSOR_REG_RO},
	{DEVICE_STATUS, "DEVICE_STATUS", EMUL_SENSOR_REG_RO},
	{DATA_AQI, "DATA_AQI", EMUL_SENSOR_REG_RO, .reset = 0x01,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{DATA_TVOC, "DATA_TVOC", EMUL_SENSOR_REG_RO, .bytes = 2,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{DATA_ECO2, "DATA_ECO2", EMUL_SENSOR_REG_RO, .bytes = 2,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{0x26, "Reserved", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x28, "Reserved", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x2a, "Reserved", EMUL_SENSOR_REG_RO, .bytes = 2},
	{0x2c, "Reserved", EMUL_SENSOR_REG_RO},
	{0x2d, "Reserved", EMUL_SENSOR_REG_RO},
	{0x2e, "Reserved", EMUL_SENSOR_REG_RO},
	{0x2f, "Reserved", EMUL_SENSOR_REG_RO},
	/* Echo of TEMP_IN (or 25 degC = (25 + 273.15) * 64 if never written), section 16.2.12. */
	{DATA_T, "DATA_T", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x4a8a,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	/* Echo of RH_IN (or 50%RH = 50 * 512 if never written), section 16.2.13. */
	{DATA_RH, "DATA_RH", EMUL_SENSOR_REG_RO, .bytes = 2, .reset = 0x6400,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{0x34, "Reserved", EMUL_SENSOR_REG_RO},
	{0x35, "Reserved", EMUL_SENSOR_REG_RO},
	{0x36, "Reserved", EMUL_SENSOR_REG_RO},
	{0x37, "Reserved", EMUL_SENSOR_REG_RO},
	{DATA_MISR, "DATA_MISR", EMUL_SENSOR_REG_RO,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{GPR_WRITE0 + 0, "GPR_WRITE0", .write_mask = 0xff},
	{GPR_WRITE0 + 1, "GPR_WRITE1", .write_mask = 0xff},
	{GPR_WRITE0 + 2, "GPR_WRITE2", .write_mask = 0xff},
	{GPR_WRITE0 + 3, "GPR_WRITE3", .write_mask = 0xff},
	{GPR_WRITE0 + 4, "GPR_WRITE4", .write_mask = 0xff},
	{GPR_WRITE0 + 5, "GPR_WRITE5", .write_mask = 0xff},
	{GPR_WRITE0 + 6, "GPR_WRITE6", .write_mask = 0xff},
	{GPR_WRITE0 + 7, "GPR_WRITE7", .write_mask = 0xff},
	/* Only the first GPR_READx read of a transaction clears NEWGPR (section 16.2.16). */
	{GPR_READ0 + 0, "GPR_READ0", EMUL_SENSOR_REG_RO,
	 .read_clears = {DEVICE_STATUS, DEVICE_STATUS_NEWGPR}},
	{GPR_READ0 + 1, "GPR_READ1", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 2, "GPR_READ2", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 3, "GPR_READ3", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 4, "GPR_READ4", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 5, "GPR_READ5", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 6, "GPR_READ6", EMUL_SENSOR_REG_RO},
	{GPR_READ0 + 7, "GPR_READ7", EMUL_SENSOR_REG_RO},
};

/*
 * DATA_TVOC and DATA_ECO2 are plain 16-bit unsigned counts at 1 ppb and 1 ppm per LSB
 * (Table 4, sections 16.2.9 and 16.2.10). DEVICE_STATUS.NEWDAT (section 16.2.7) is the single
 * data-ready flag shared by all DATA_x outputs: it is set here when a sample is produced and
 * cleared by the read_clears on each DATA_x register above.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_CO2, .reg = DATA_ECO2, .bits = 16, .lsb = 1.0,
	 .min = 400, .max = 65000,
	 .ready = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
	{.chan = SENSOR_CHAN_VOC, .reg = DATA_TVOC, .bits = 16, .lsb = 1.0,
	 .min = 0, .max = 65000,
	 .ready = {DEVICE_STATUS, DEVICE_STATUS_NEWDAT}},
};

/*
 * STANDARD (OPMODE 0x02) runs gas sensing continuously; the device does not wait for a
 * one-shot command. Entering it from IDLE or DEEP SLEEP does not set any bit that stays 1
 * (OPMODE is a mode value, not a set of independent control bits), so convert_on_write cannot
 * express the transition: trigger the conversion explicitly, as with STTS751 and TMP451.
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (addr == OPMODE && data->regs[OPMODE] == OPMODE_STANDARD) {
		emul_sensor_regmap_convert(target);
	}
}

/*
 * Gas sensing is suppressed outside STANDARD mode (section 15); masking out bit 0 makes
 * DEEP_SLEEP (0x00) and IDLE (0x01) match as one condition, matching neither STANDARD (0x02)
 * nor the transient RESET command (0xf0), which reset_on_write above restores to IDLE anyway.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.byte_addressed = true,
	.disabled = {.reg = OPMODE, .mask = 0xfe, .value = OPMODE_DEEP_SLEEP},
	.write = write);
