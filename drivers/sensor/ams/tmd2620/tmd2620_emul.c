/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT ams_tmd2620

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * ams TMD2620 Proximity Sensor Module datasheet, ams Datasheet [v1-02] 2021-Jun-02,
 * "Register Description" (pages 8-21), device address 0x29:
 * https://docs.rs-online.com/bb51/A700000008290167.pdf
 *
 * Figure 11 (Register Overview) and the per-register field tables disagree on the reset value
 * of CALIBCFG (0x00 in Figure 11 versus 0x50 computed from the field table of Figure 30, which
 * the revision history records as updated in this document revision while Figure 11 was not);
 * the field table is used below. CFG2 (0x9F) appears only in Figure 11, with no bit-level
 * description anywhere in this revision.
 */
enum {
	ENABLE = 0x80,
	PRATE = 0x82,
	WTIME = 0x83,
	PILT = 0x88,
	PIHT = 0x8A,
	PERS = 0x8C,
	CFG0 = 0x8D,
	PCFG0 = 0x8E,
	PCFG1 = 0x8F,
	REVID = 0x91,
	ID = 0x92,
	STATUS = 0x93,
	PDATA = 0x9C,
	REVID2 = 0x9E,
	CFG2 = 0x9F,
	CFG3 = 0xAB,
	POFFSET_L = 0xC0,
	POFFSET_H = 0xC1,
	CALIB = 0xD7,
	CALIBCFG = 0xD9,
	CALIBSTAT = 0xDC,
	INTENAB = 0xDD,
};

/* ENABLE (0x80) */
#define ENABLE_WEN BIT(3) /* Wait Enable */
#define ENABLE_PEN BIT(2) /* Proximity Detect Enable */
#define ENABLE_PON BIT(0) /* Power On: internal oscillator, timers and ADC */

/* STATUS (0x93), all "R, SC": writing a 1 to a flag clears it (Figure 23) */
#define STATUS_PSAT BIT(6)
#define STATUS_PINT BIT(5)
#define STATUS_CINT BIT(3)
#define STATUS_ZINT BIT(2)
#define STATUS_PSAT_REFLECTIVE BIT(1)
#define STATUS_PSAT_AMBIENT BIT(0)
#define STATUS_W1C_MASK                                                                        \
	(STATUS_PSAT | STATUS_PINT | STATUS_CINT | STATUS_ZINT | STATUS_PSAT_REFLECTIVE |       \
	 STATUS_PSAT_AMBIENT)

/* CFG3 (0xAB) */
#define CFG3_INT_READ_CLEAR BIT(7)
#define CFG3_SAI BIT(4)

/* CALIB (0xD7) */
#define CALIB_ELECTRICAL_CALIBRATION BIT(5)
#define CALIB_START_OFFSET_CALIB BIT(0)

/* CALIBCFG (0xD9) */
#define CALIBCFG_PROX_AUTO_OFFSET_ADJUST BIT(3)

/* CALIBSTAT (0xDC) */
#define CALIBSTAT_OFFSET_ADJUSTED BIT(2)
#define CALIBSTAT_CALIB_FINISHED BIT(0)

/* INTENAB (0xDD) */
#define INTENAB_PSIEN BIT(6)
#define INTENAB_PIEN BIT(5)
#define INTENAB_CIEN BIT(3)
#define INTENAB_ZIEN BIT(2)

static const struct emul_sensor_reg registers[] = {
	{ENABLE, "ENABLE", .write_mask = ENABLE_WEN | ENABLE_PEN | ENABLE_PON,
	 .convert_on_write = ENABLE_PEN | ENABLE_PON},
	{PRATE, "PRATE", .reset = 0x1F},
	{WTIME, "WTIME"},
	{PILT, "PILT"},
	{PIHT, "PIHT"},
	/* ppers<7:4>; bits 3:0 reserved */
	{PERS, "PERS", .write_mask = 0xF0},
	/* Reserved<7:3> reads/must stay 10000, wlong is bit 2, Reserved<1:0> must stay 00 */
	{CFG0, "CFG0", .reset = 0x80, .write_mask = BIT(2)},
	/* ppulse_len<7:6>=1 (8us), ppulse<5:0>=15 (16 pulses): 0x4F */
	{PCFG0, "PCFG0", .reset = 0x4F},
	/* pgain<7:6>=2 (4x), reserved bit 5, pldrive<4:0>=0 (6mA): 0x80 */
	{PCFG1, "PCFG1", .reset = 0x80, .write_mask = 0xC0 | 0x1F},
	/* Factory-programmed silicon revision; left blank in Figure 11 */
	{REVID, "REVID", EMUL_SENSOR_REG_RO},
	/* id<7:2> = 110101b identifies TMD2620, reserved<1:0> = 00: 0xD4 */
	{ID, "ID", EMUL_SENSOR_REG_RO, .reset = 0xD4},
	{STATUS, "STATUS", .write_mask = STATUS_W1C_MASK},
	{PDATA, "PDATA", EMUL_SENSOR_REG_RO},
	/* aux_id<3:0> is documented as "TBD"; reserved<7:4> */
	{REVID2, "REVID2", EMUL_SENSOR_REG_RO},
	/* No bit-level description exists for this revision; kept as an opaque byte. */
	{CFG2, "CFG2"},
	/* int_read_clear is bit 7, sai is bit 4, reserved<6:5>="R, SC", reserved<3:0> must
	 * stay 1100: 0x0C
	 */
	{CFG3, "CFG3", .reset = 0x0C, .write_mask = CFG3_INT_READ_CLEAR | CFG3_SAI},
	/* Factory-calibrated offset magnitude; treated as a plain read/write byte, despite
	 * the field table listing type "R, SC" while Figure 11 lists it as R/W.
	 */
	{POFFSET_L, "POFFSET_L"},
	/* Only bit 0 (sign) is documented; bits 7:1 are unspecified in this revision. */
	{POFFSET_H, "POFFSET_H", .write_mask = 0x01},
	/* electrical_calibration is bit 5, start_offset_calib is bit 0; reserved<7:6,4:1> */
	{CALIB, "CALIB", .write_mask = CALIB_ELECTRICAL_CALIBRATION | CALIB_START_OFFSET_CALIB},
	/* binsrch_target<7:5>=2, reserved bit 4 must stay 1, prox_auto_offset_adjust is bit 3,
	 * prx_data_avg<2:0>=0 (disabled): 0x50 (see the header comment on the Figure 11/30
	 * reset-value mismatch)
	 */
	{CALIBCFG, "CALIBCFG", .reset = 0x50,
	 .write_mask = 0xE0 | CALIBCFG_PROX_AUTO_OFFSET_ADJUST | 0x07},
	{CALIBSTAT, "CALIBSTAT", .write_mask = CALIBSTAT_CALIB_FINISHED},
	{INTENAB, "INTENAB",
	 .write_mask = INTENAB_PSIEN | INTENAB_PIEN | INTENAB_CIEN | INTENAB_ZIEN},
};

/*
 * PDATA (0x9C) is the only measurement register: an 8-bit unsigned proximity result with no
 * documented physical unit (Figure 8, Figure 9). SENSOR_CHAN_PROX is dimensionless with 1
 * meaning "close" (sensor_channel.h), so the full 8-bit code is normalized to 0-1.
 *
 * With PERS.ppers at its reset value of 0, "Interrupt generated when: Every proximity cycle"
 * (Figure 17): pint is asserted after every proximity conversion regardless of the PILT/PIHT
 * thresholds, so it is modeled as an unconditional data-ready flag for the default
 * configuration. PGAIN/PLDRIVE (PCFG1) change the sensor's optical sensitivity but the
 * datasheet gives no counts-per-unit constant for them, so they are not modeled as .select
 * variants.
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_PROX, .reg = PDATA, .bits = 8, .lsb = 1.0 / 255, .min = 0, .max = 1,
	 .ready = {.reg = STATUS, .mask = STATUS_PINT},
	 .disabled = {.reg = ENABLE, .mask = ENABLE_PEN, .value = 0}},
};

/*
 * True write-one-to-clear: after the framework merges the write, a bit within mask holds
 * exactly what was written (1 or 0), since write_mask limits changes to that mask. Bits
 * written as 1 clear to 0; bits written as 0 keep their previous value instead of also being
 * forced to 0, which the plain write_mask merge alone would do.
 */
static void w1c(struct emul_sensor_regmap_data *data, uint8_t reg, uint32_t old, uint32_t mask)
{
	uint32_t clear = data->regs[reg] & mask;

	data->regs[reg] = (old & ~clear) | (data->regs[reg] & ~mask);
}

/*
 * CALIB.electrical_calibration and CALIB.start_offset_calib each start an offset calibration
 * (Figure 29); CALIBCFG.prox_auto_offset_adjust starts an automatic offset decrement
 * (Figure 30). All three are momentary commands with no self_clear entry in the register
 * table: conversions complete instantaneously in this model, so the command bit is cleared
 * and the corresponding CALIBSTAT flag set within the same write, rather than relying on
 * self_clear ordering against this callback. The resulting POFFSET_L/H value that a real
 * calibration would compute is not simulated.
 */
static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	switch (reg) {
	case STATUS:
		w1c(data, STATUS, old, STATUS_W1C_MASK);
		break;
	case CALIBSTAT:
		w1c(data, CALIBSTAT, old, CALIBSTAT_CALIB_FINISHED);
		break;
	case CALIB:
		if ((data->regs[CALIB] &
		     (CALIB_ELECTRICAL_CALIBRATION | CALIB_START_OFFSET_CALIB)) != 0U) {
			data->regs[CALIB] &=
				~(uint32_t)(CALIB_ELECTRICAL_CALIBRATION |
					    CALIB_START_OFFSET_CALIB);
			data->regs[CALIBSTAT] |= CALIBSTAT_CALIB_FINISHED;
		}
		break;
	case CALIBCFG:
		if ((data->regs[CALIBCFG] & CALIBCFG_PROX_AUTO_OFFSET_ADJUST) != 0U) {
			data->regs[CALIBCFG] &= ~(uint32_t)CALIBCFG_PROX_AUTO_OFFSET_ADJUST;
			data->regs[CALIBSTAT] |= CALIBSTAT_OFFSET_ADJUSTED;
		}
		break;
	default:
		break;
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.disabled = {.reg = ENABLE, .mask = ENABLE_PON, .value = 0}, .write = write);
