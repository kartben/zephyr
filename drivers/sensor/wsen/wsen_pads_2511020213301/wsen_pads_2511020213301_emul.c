/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT we_wsen_pads_2511020213301

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * Wurth Elektronik eiSos, "Absolute Pressure Sensor WSEN-PADS 2511020213301, User Manual",
 * version 1.0, June 18 2019, sections 11-12 (register map and register description):
 * https://www.mouser.com/pdfDocs/WSEN-PADS_2511020213301.pdf
 *
 * FIFO buffer contents (0x78-0x7C), the differential-pressure engine (AUTOZERO/AUTOREFP,
 * REF_P_x, OPC_x one-point offset compensation), pressure-threshold interrupt comparison and
 * INT pin routing are not modeled: only their registers are declared for storage. The
 * measurement channels use the raw, uncompensated data path.
 */
enum {
	INT_CFG = 0x0b, THR_P_L = 0x0c, THR_P_H = 0x0d, INTERFACE_CTRL = 0x0e, DEVICE_ID = 0x0f,
	CTRL_1 = 0x10, CTRL_2 = 0x11, CTRL_3 = 0x12, FIFO_CTRL = 0x13, FIFO_WTM = 0x14,
	REF_P_L = 0x15, REF_P_H = 0x16, OPC_L = 0x18, OPC_H = 0x19,
	INT_SOURCE = 0x24, FIFO_STATUS_1 = 0x25, FIFO_STATUS_2 = 0x26, STATUS = 0x27,
	DATA_P_XL = 0x28, DATA_P_L = 0x29, DATA_P_H = 0x2a, DATA_T_L = 0x2b, DATA_T_H = 0x2c,
	FIFO_DATA_P_XL = 0x78, FIFO_DATA_P_L = 0x79, FIFO_DATA_P_H = 0x7a,
	FIFO_DATA_T_L = 0x7b, FIFO_DATA_T_H = 0x7c,
};

static const struct emul_sensor_reg registers[] = {
	/* AUTOREFP and AUTOZERO (bits 7 and 5) self clear after the first measurement */
	{INT_CFG, "INT_CFG", .write_mask = 0xff, .self_clear = BIT(7) | BIT(5)},
	{THR_P_L, "THR_P_L", .write_mask = 0xff},
	/* Bit 7 is reserved; THR[14:8] is a 15-bit unsigned differential pressure threshold */
	{THR_P_H, "THR_P_H", .write_mask = 0x7f},
	/* Bits 3:0 are reserved */
	{INTERFACE_CTRL, "INTERFACE_CTRL", .write_mask = 0xf0},
	{DEVICE_ID, "DEVICE_ID", EMUL_SENSOR_REG_RO, .reset = 0xb3},
	/* Bits 7 and 0 are reserved */
	{CTRL_1, "CTRL_1", .write_mask = 0x7e},
	/*
	 * Bit 3 is reserved. IF_ADD_INC (bit 4) is enabled at reset (section 12.7). BOOT (bit 7),
	 * SWRESET (bit 2) and ONE_SHOT (bit 0) are commands that self clear once processed.
	 */
	{CTRL_2, "CTRL_2", .reset = 0x10, .write_mask = 0xf7,
	 .self_clear = BIT(7) | BIT(2) | BIT(0), .convert_on_write = BIT(0),
	 .requires_standby = true},
	/* Bits 7:6 are reserved */
	{CTRL_3, "CTRL_3", .write_mask = 0x3f},
	/* Bits 7:4 are reserved */
	{FIFO_CTRL, "FIFO_CTRL", .write_mask = 0x0f},
	/* Bit 7 is reserved; maximum allowed threshold is 0x7f (section 12.10) */
	{FIFO_WTM, "FIFO_WTM", .write_mask = 0x7f},
	/* Type R in the register description (section 12.11), despite R/W in the map (11) */
	{REF_P_L, "REF_P_L", EMUL_SENSOR_REG_RO},
	{REF_P_H, "REF_P_H", EMUL_SENSOR_REG_RO},
	{OPC_L, "OPC_L", .write_mask = 0xff},
	{OPC_H, "OPC_H", .write_mask = 0xff},
	{INT_SOURCE, "INT_SOURCE", EMUL_SENSOR_REG_RO},
	{FIFO_STATUS_1, "FIFO_STATUS_1", EMUL_SENSOR_REG_RO},
	{FIFO_STATUS_2, "FIFO_STATUS_2", EMUL_SENSOR_REG_RO},
	/* P_DA/P_OR (bits 0, 4) and T_DA/T_OR (bits 1, 5) are cleared by reading DATA_P_H/T_H */
	{STATUS, "STATUS", EMUL_SENSOR_REG_RO},
	{DATA_P_XL, "DATA_P_XL", EMUL_SENSOR_REG_RO},
	{DATA_P_L, "DATA_P_L", EMUL_SENSOR_REG_RO},
	{DATA_P_H, "DATA_P_H", EMUL_SENSOR_REG_RO, .read_clears = {.reg = STATUS, .mask = 0x11}},
	{DATA_T_L, "DATA_T_L", EMUL_SENSOR_REG_RO},
	{DATA_T_H, "DATA_T_H", EMUL_SENSOR_REG_RO, .read_clears = {.reg = STATUS, .mask = 0x22}},
	{FIFO_DATA_P_XL, "FIFO_DATA_P_XL", EMUL_SENSOR_REG_RO},
	{FIFO_DATA_P_L, "FIFO_DATA_P_L", EMUL_SENSOR_REG_RO},
	{FIFO_DATA_P_H, "FIFO_DATA_P_H", EMUL_SENSOR_REG_RO},
	{FIFO_DATA_T_L, "FIFO_DATA_T_L", EMUL_SENSOR_REG_RO},
	{FIFO_DATA_T_H, "FIFO_DATA_T_H", EMUL_SENSOR_REG_RO},
};

/*
 * DATA_P_XL/L/H (0x28-0x2A) hold a 24-bit two's complement pressure word, DATA_P_H being the
 * most significant byte; sensitivity SENP = 1/40960 kPa/digit is already in kPa (table 4,
 * section 8.1). DATA_T_L/H (0x2B-0x2C) hold a 16-bit two's complement temperature word;
 * sensitivity SENT = 0.01 degC/digit (table 5, section 8.2). Resolution and sensitivity do not
 * depend on ODR or the low-power/low-noise setting (tables 4 and 5), so there is no variant.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_PRESS, .reg = DATA_P_XL, .is_signed = true, .bits = 24,
	 .lsb = 1.0 / 40960, .min = 26, .max = 126,
	 .ready = {.reg = STATUS, .mask = BIT(0)}, .overrun = BIT(4)},
	{.chan = SENSOR_CHAN_AMBIENT_TEMP, .reg = DATA_T_L, .is_signed = true, .bits = 16,
	 .lsb = 0.01, .min = -40, .max = 85,
	 .ready = {.reg = STATUS, .mask = BIT(1)}, .overrun = BIT(5)},
};

/*
 * SWRESET (CTRL_2 bit 2) restores the registers listed in section 6.4. BOOT (CTRL_2 bit 7)
 * reloads trimming parameters from NVM and, per section 6.3, resets the OPC_L/OPC_H offset
 * compensation registers to zero.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;
	static const uint8_t reset_regs[] = {
		INT_CFG, THR_P_L, THR_P_H, INTERFACE_CTRL, CTRL_1, CTRL_2, CTRL_3,
		FIFO_CTRL, FIFO_WTM, INT_SOURCE, FIFO_STATUS_1, FIFO_STATUS_2, STATUS,
	};

	ARG_UNUSED(old);
	if (reg != CTRL_2) {
		return;
	}
	if ((data->regs[CTRL_2] & BIT(2)) != 0U) {
		for (size_t i = 0; i < ARRAY_SIZE(reset_regs); i++) {
			for (size_t j = 0; j < ARRAY_SIZE(registers); j++) {
				if (registers[j].addr == reset_regs[i]) {
					data->regs[reset_regs[i]] = registers[j].reset;
				}
			}
		}
	}
	if ((data->regs[CTRL_2] & BIT(7)) != 0U) {
		data->regs[OPC_L] = 0;
		data->regs[OPC_H] = 0;
	}
}

/* Power-down/one-shot mode is ODR[2:0] = 0 in CTRL_1 (section 7.1); BDU is CTRL_1 bit 1 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.increment = {CTRL_2, BIT(4)},
	.disabled = {.reg = CTRL_1, .mask = 0x70, .value = 0},
	.block_update = {.reg = CTRL_1, .mask = BIT(1), .value = BIT(1)},
	.write = write);
