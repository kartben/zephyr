/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT maxim_max17055

#include <zephyr/drivers/emul_sensor_regmap.h>

/*
 * 19-8726; Rev 0; 1/17, Table 16 (MAX17055 Register Memory Map):
 * https://www.analog.com/media/en/technical-documentation/data-sheets/max17055.pdf
 *
 * Register formats and reset values not detailed in the data sheet come from the companion
 * MAX17055 ModelGauge m5 EZ User Guide, Rev.0, December 2016, Table 10 (MAX17055 Register Map)
 * and sections 1.9.1 to 1.9.5 and 1.10:
 * https://www.analog.com/media/en/technical-documentation/user-guides/max17055-user-guide.pdf
 *
 * All registers are 16-bit words, least significant byte first on the bus, and the pointer
 * auto-increments by one word per access (user guide section 1.12.3). Slave address 0x6C
 * (8-bit) / 0x36 (7-bit).
 *
 * Current, capacity and power registers store a voltage (or accumulated voltage) that the host
 * must divide by the external sense resistor value to obtain amps, amp-hours or watts (user
 * guide section 1.3); that resistor is a board-specific hardware constant, not a data sheet
 * LSB, so those registers are transcribed here but not exposed as sensor channels. Voltage,
 * temperature, percentage, time and cycle registers have a fixed LSB and are modeled below.
 *
 * Registers named "Reserved" in the memory map and the OCVTable/XTable cell-characterization
 * tables (80h-9Fh) are transcribed with a reset of 0 and no known format; the data sheet says
 * not to write reserved locations and gives no format for the characterization tables. VFOCV
 * (FBh) and VFSOC (FFh) are documented individually in the text but fall outside the printed
 * memory-map grid; addresses elsewhere in 00h-FFh are not part of the documented memory space
 * and are omitted, per the framework's -EIO-on-unlisted-address behavior.
 */
enum {
	STATUS = 0x00,
	VALRTTH = 0x01,
	TALRTTH = 0x02,
	SALRTTH = 0x03,
	ATRATE = 0x04,
	REPCAP = 0x05,
	REPSOC = 0x06,
	AGE = 0x07,
	TEMP = 0x08,
	VCELL = 0x09,
	CURRENT = 0x0a,
	AVGCURRENT = 0x0b,
	QRESIDUAL = 0x0c,
	MIXSOC = 0x0d,
	AVSOC = 0x0e,
	MIXCAP = 0x0f,
	FULLCAPREP = 0x10,
	TTE = 0x11,
	QRTABLE00 = 0x12,
	FULLSOCTHR = 0x13,
	RCELL = 0x14,
	RESERVED_15 = 0x15,
	AVGTA = 0x16,
	CYCLES = 0x17,
	DESIGNCAP = 0x18,
	AVGVCELL = 0x19,
	MAXMINTEMP = 0x1a,
	MAXMINVOLT = 0x1b,
	MAXMINCURR = 0x1c,
	CONFIG = 0x1d,
	ICHGTERM = 0x1e,
	AVCAP = 0x1f,
	TTF = 0x20,
	DEVNAME = 0x21,
	QRTABLE10 = 0x22,
	FULLCAPNOM = 0x23,
	RESERVED_24 = 0x24,
	RESERVED_25 = 0x25,
	RESERVED_26 = 0x26,
	AIN = 0x27,
	LEARNCFG = 0x28,
	FILTERCFG = 0x29,
	RELAXCFG = 0x2a,
	MISCCFG = 0x2b,
	TGAIN = 0x2c,
	TOFF = 0x2d,
	CGAIN = 0x2e,
	COFF = 0x2f,
	RESERVED_30 = 0x30,
	RESERVED_31 = 0x31,
	QRTABLE20 = 0x32,
	RESERVED_33 = 0x33,
	DIETEMP = 0x34,
	FULLCAP = 0x35,
	RESERVED_36 = 0x36,
	RESERVED_37 = 0x37,
	RCOMP0 = 0x38,
	TEMPCO = 0x39,
	VEMPTY = 0x3a,
	RESERVED_3B = 0x3b,
	RESERVED_3C = 0x3c,
	FSTAT = 0x3d,
	TIMER = 0x3e,
	SHDNTIMER = 0x3f,
	USERMEM1 = 0x40,
	RESERVED_41 = 0x41,
	QRTABLE30 = 0x42,
	RGAIN = 0x43,
	RESERVED_44 = 0x44,
	DQACC = 0x45,
	DPACC = 0x46,
	RESERVED_47 = 0x47,
	RESERVED_48 = 0x48,
	CONVGCFG = 0x49,
	VFREMCAP = 0x4a,
	RESERVED_4B = 0x4b,
	RESERVED_4C = 0x4c,
	QH = 0x4d,
	RESERVED_4E = 0x4e,
	RESERVED_4F = 0x4f,
	OCVTABLE0 = 0x80,
	OCVTABLE1 = 0x81,
	OCVTABLE2 = 0x82,
	OCVTABLE3 = 0x83,
	OCVTABLE4 = 0x84,
	OCVTABLE5 = 0x85,
	OCVTABLE6 = 0x86,
	OCVTABLE7 = 0x87,
	OCVTABLE8 = 0x88,
	OCVTABLE9 = 0x89,
	OCVTABLE10 = 0x8a,
	OCVTABLE11 = 0x8b,
	OCVTABLE12 = 0x8c,
	OCVTABLE13 = 0x8d,
	OCVTABLE14 = 0x8e,
	OCVTABLE15 = 0x8f,
	XTABLE0 = 0x90,
	XTABLE1 = 0x91,
	XTABLE2 = 0x92,
	XTABLE3 = 0x93,
	XTABLE4 = 0x94,
	XTABLE5 = 0x95,
	XTABLE6 = 0x96,
	XTABLE7 = 0x97,
	XTABLE8 = 0x98,
	XTABLE9 = 0x99,
	XTABLE10 = 0x9a,
	XTABLE11 = 0x9b,
	XTABLE12 = 0x9c,
	XTABLE13 = 0x9d,
	XTABLE14 = 0x9e,
	XTABLE15 = 0x9f,
	STATUS2 = 0xb0,
	POWER = 0xb1,
	ID_USERMEM2 = 0xb2,
	AVGPOWER = 0xb3,
	IALRTTH = 0xb4,
	TTFCFG = 0xb5,
	CVMIXCAP = 0xb6,
	CVHALFTIME = 0xb7,
	CGTEMPCO = 0xb8,
	CURVE = 0xb9,
	HIBCFG = 0xba,
	CONFIG2 = 0xbb,
	VRIPPLE = 0xbc,
	RIPPLECFG = 0xbd,
	TIMERH = 0xbe,
	RESERVED_BF = 0xbf,
	RSENSE_USERMEM3 = 0xd0,
	SCOCVLIM = 0xd1,
	RESERVED_D2 = 0xd2,
	SOCHOLD = 0xd3,
	MAXPEAKPOWER = 0xd4,
	SUSPEAKPOWER = 0xd5,
	PACKRESISTANCE = 0xd6,
	SYSRESISTANCE = 0xd7,
	MINSYSVOLTAGE = 0xd8,
	MPPCURRENT = 0xd9,
	SPPCURRENT = 0xda,
	MODELCFG = 0xdb,
	ATQRESIDUAL = 0xdc,
	ATTTE = 0xdd,
	ATAVSOC = 0xde,
	ATAVCAP = 0xdf,
	VFOCV = 0xfb,
	VFSOC = 0xff,
};

/* SHDN, Config (1Dh) bit 7: user guide Table 21. Forces shutdown; all activity stops. */
#define CONFIG_SHDN BIT(7)
/* DNR, FStat (3Dh) bit 0: set to 1 at cell insertion/reset, cleared once outputs are valid. */
#define FSTAT_DNR BIT(0)

static const struct emul_sensor_reg registers[] = {
	{STATUS, "Status", .reset = 0x0002},
	{VALRTTH, "VAlrtTh", .reset = 0xff00},
	{TALRTTH, "TAlrtTh", .reset = 0x7f80},
	{SALRTTH, "SAlrtTh", .reset = 0xff00},
	{ATRATE, "AtRate"},
	{REPCAP, "RepCap", EMUL_SENSOR_REG_RO},
	{REPSOC, "RepSOC", EMUL_SENSOR_REG_RO},
	{AGE, "Age", EMUL_SENSOR_REG_RO},
	{TEMP, "Temp", EMUL_SENSOR_REG_RO},
	{VCELL, "VCell", EMUL_SENSOR_REG_RO},
	{CURRENT, "Current", EMUL_SENSOR_REG_RO},
	{AVGCURRENT, "AvgCurrent", EMUL_SENSOR_REG_RO},
	{QRESIDUAL, "QResidual", EMUL_SENSOR_REG_RO},
	{MIXSOC, "MixSOC", EMUL_SENSOR_REG_RO},
	{AVSOC, "AvSOC", EMUL_SENSOR_REG_RO},
	{MIXCAP, "MixCap", EMUL_SENSOR_REG_RO},
	{FULLCAPREP, "FullCapRep", EMUL_SENSOR_REG_RO},
	{TTE, "TTE", EMUL_SENSOR_REG_RO},
	{QRTABLE00, "QRTable00"},
	/* Percentage register whose 3 low bits are fixed at 101b (user guide Table 13). */
	{FULLSOCTHR, "FullSOCThr", .reset = 0x5f05, .write_mask = 0xfff8},
	{RCELL, "RCell", EMUL_SENSOR_REG_RO, .reset = 0x0290},
	{RESERVED_15, "Reserved", EMUL_SENSOR_REG_RO},
	{AVGTA, "AvgTA", EMUL_SENSOR_REG_RO},
	{CYCLES, "Cycles", EMUL_SENSOR_REG_RO},
	{DESIGNCAP, "DesignCap"},
	{AVGVCELL, "AvgVCell", EMUL_SENSOR_REG_RO},
	{MAXMINTEMP, "MaxMinTemp", .reset = 0x807f},
	{MAXMINVOLT, "MaxMinVolt", .reset = 0x00ff},
	{MAXMINCURR, "MaxMinCurr", .reset = 0x807f},
	/* Bit 5 is fixed at 0 (user guide Table 21); all other bits are host-writable. */
	{CONFIG, "Config", .reset = 0x2210, .write_mask = 0xffdf},
	{ICHGTERM, "IChgTerm", .reset = 0x0640},
	{AVCAP, "AvCap", EMUL_SENSOR_REG_RO},
	{TTF, "TTF", EMUL_SENSOR_REG_RO},
	{DEVNAME, "DevName", EMUL_SENSOR_REG_RO, .reset = 0x4010},
	{QRTABLE10, "QRTable10"},
	{FULLCAPNOM, "FullCapNom", EMUL_SENSOR_REG_RO},
	{RESERVED_24, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_25, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_26, "Reserved", EMUL_SENSOR_REG_RO},
	{AIN, "AIN", EMUL_SENSOR_REG_RO},
	{LEARNCFG, "LearnCfg", .reset = 0x4486},
	{FILTERCFG, "FilterCfg", .reset = 0xcea4},
	{RELAXCFG, "RelaxCfg", .reset = 0x2039},
	{MISCCFG, "MiscCfg", .reset = 0x3870},
	{TGAIN, "TGain", .reset = 0xee56},
	{TOFF, "TOff", .reset = 0x1da4},
	{CGAIN, "CGain", .reset = 0x0400},
	{COFF, "COff"},
	{RESERVED_30, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_31, "Reserved", EMUL_SENSOR_REG_RO},
	{QRTABLE20, "QRTable20"},
	{RESERVED_33, "Reserved", EMUL_SENSOR_REG_RO},
	{DIETEMP, "DieTemp", EMUL_SENSOR_REG_RO},
	{FULLCAP, "FullCap", EMUL_SENSOR_REG_RO},
	{RESERVED_36, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_37, "Reserved", EMUL_SENSOR_REG_RO},
	{RCOMP0, "RComp0"},
	{TEMPCO, "TempCo"},
	{VEMPTY, "VEmpty", .reset = 0xa561},
	{RESERVED_3B, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_3C, "Reserved", EMUL_SENSOR_REG_RO},
	/*
	 * The datasheet powers up with DNR set for the ~710 ms it takes the first measurement to
	 * complete. Conversions are instantaneous in this model and there is no notion of that
	 * window, so DNR resets clear; a driver polling it at init would otherwise spin forever.
	 */
	{FSTAT, "FStat", EMUL_SENSOR_REG_RO},
	{TIMER, "Timer", EMUL_SENSOR_REG_RO},
	{SHDNTIMER, "ShdnTimer"},
	{USERMEM1, "UserMem1"},
	{RESERVED_41, "Reserved", EMUL_SENSOR_REG_RO},
	{QRTABLE30, "QRTable30"},
	{RGAIN, "RGain", .reset = 0x8080},
	{RESERVED_44, "Reserved", EMUL_SENSOR_REG_RO},
	{DQACC, "dQAcc", .reset = 0x0017},
	{DPACC, "dPAcc", .reset = 0x0190},
	{RESERVED_47, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_48, "Reserved", EMUL_SENSOR_REG_RO},
	{CONVGCFG, "ConvgCfg", .reset = 0x2241},
	{VFREMCAP, "VFRemCap", EMUL_SENSOR_REG_RO},
	{RESERVED_4B, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_4C, "Reserved", EMUL_SENSOR_REG_RO},
	{QH, "QH", EMUL_SENSOR_REG_RO},
	{RESERVED_4E, "Reserved", EMUL_SENSOR_REG_RO},
	{RESERVED_4F, "Reserved", EMUL_SENSOR_REG_RO},
	/* 80h-9Fh: cell characterization tables loaded from battery-specific data (user guide
	 * section 1.9.1); no published reset value or bit format.
	 */
	{OCVTABLE0, "OCVTable0"}, {OCVTABLE1, "OCVTable1"}, {OCVTABLE2, "OCVTable2"},
	{OCVTABLE3, "OCVTable3"}, {OCVTABLE4, "OCVTable4"}, {OCVTABLE5, "OCVTable5"},
	{OCVTABLE6, "OCVTable6"}, {OCVTABLE7, "OCVTable7"}, {OCVTABLE8, "OCVTable8"},
	{OCVTABLE9, "OCVTable9"}, {OCVTABLE10, "OCVTable10"}, {OCVTABLE11, "OCVTable11"},
	{OCVTABLE12, "OCVTable12"}, {OCVTABLE13, "OCVTable13"}, {OCVTABLE14, "OCVTable14"},
	{OCVTABLE15, "OCVTable15"},
	{XTABLE0, "XTable0"}, {XTABLE1, "XTable1"}, {XTABLE2, "XTable2"}, {XTABLE3, "XTable3"},
	{XTABLE4, "XTable4"}, {XTABLE5, "XTable5"}, {XTABLE6, "XTable6"}, {XTABLE7, "XTable7"},
	{XTABLE8, "XTable8"}, {XTABLE9, "XTable9"}, {XTABLE10, "XTable10"},
	{XTABLE11, "XTable11"}, {XTABLE12, "XTable12"}, {XTABLE13, "XTable13"},
	{XTABLE14, "XTable14"}, {XTABLE15, "XTable15"},
	{STATUS2, "Status2", EMUL_SENSOR_REG_RO},
	{POWER, "Power", EMUL_SENSOR_REG_RO},
	{ID_USERMEM2, "ID/UserMem2"},
	{AVGPOWER, "AvgPower", EMUL_SENSOR_REG_RO},
	{IALRTTH, "IAlrtTh", .reset = 0x7f80},
	{TTFCFG, "TTFCfg"},
	{CVMIXCAP, "CVMixCap", EMUL_SENSOR_REG_RO},
	{CVHALFTIME, "CVHalfTime", EMUL_SENSOR_REG_RO},
	{CGTEMPCO, "CGTempCo"},
	{CURVE, "Curve", .reset = 0x0025},
	{HIBCFG, "HibCfg", .reset = 0x870c},
	{CONFIG2, "Config2", .reset = 0x3658},
	{VRIPPLE, "VRipple", EMUL_SENSOR_REG_RO},
	{RIPPLECFG, "RippleCfg", .reset = 0x0204},
	{TIMERH, "TimerH", EMUL_SENSOR_REG_RO},
	{RESERVED_BF, "Reserved", EMUL_SENSOR_REG_RO},
	{RSENSE_USERMEM3, "RSense/UserMem3"},
	{SCOCVLIM, "ScOcvLim", .reset = 0x479e},
	{RESERVED_D2, "Reserved", EMUL_SENSOR_REG_RO},
	{SOCHOLD, "SOCHold", .reset = 0x1002},
	{MAXPEAKPOWER, "MaxPeakPower", EMUL_SENSOR_REG_RO},
	{SUSPEAKPOWER, "SusPeakPower", EMUL_SENSOR_REG_RO},
	{PACKRESISTANCE, "PackResistance"},
	{SYSRESISTANCE, "SysResistance"},
	{MINSYSVOLTAGE, "MinSysVoltage", .reset = 0x9600},
	{MPPCURRENT, "MPPCurrent", EMUL_SENSOR_REG_RO},
	{SPPCURRENT, "SPPCurrent", EMUL_SENSOR_REG_RO},
	/* Refresh (bit 15) is a command bit: the part clears it when the refresh completes. */
	{MODELCFG, "ModelCfg", .self_clear = BIT(15)},
	{ATQRESIDUAL, "AtQResidual", EMUL_SENSOR_REG_RO},
	{ATTTE, "AtTTE", EMUL_SENSOR_REG_RO},
	{ATAVSOC, "AtAvSOC", EMUL_SENSOR_REG_RO},
	{ATAVCAP, "AtAvCap", EMUL_SENSOR_REG_RO},
	{VFOCV, "VFOCV", EMUL_SENSOR_REG_RO},
	{VFSOC, "VFSOC", EMUL_SENSOR_REG_RO},
};

/*
 * Standard register formats (user guide Table 3 / data sheet Table 6):
 *   Voltage:    0.078125mV/LSb,  16-bit unsigned, 0 to 5.11992V
 *   Percentage: 1/256 %/LSb,     16-bit unsigned, 0 to 255.9961%
 *   Time:       5.625s/LSb,      16-bit unsigned, 0 to 102.3984h
 *   Temperature: 1/256 degC/LSb, 16-bit two's complement, -128.0 to 127.996 degC
 * Cycles (17h) has a dedicated 1% LSb (0.01 cycle), 0 to 655.35 cycles (data sheet, Cycles
 * Register (17h)). TTE/TTF minutes = register x 5.625s / 60. Current, capacity and power
 * registers are excluded: their physical value additionally requires the external sense
 * resistor (RSENSE), a board constant with no fixed data sheet LSB (see file header).
 */
static const struct emul_sensor_channel channels[] = {
	{.chan = SENSOR_CHAN_GAUGE_VOLTAGE, .reg = VCELL, .bits = 16, .lsb = 0.000078125,
	 .min = 0.0, .max = 5.11992},
	{.chan = SENSOR_CHAN_GAUGE_TEMP, .reg = TEMP, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 256, .min = -128.0, .max = 127.996},
	{.chan = SENSOR_CHAN_GAUGE_STATE_OF_CHARGE, .reg = REPSOC, .bits = 16, .lsb = 1.0 / 256,
	 .min = 0.0, .max = 255.9961},
	{.chan = SENSOR_CHAN_GAUGE_STATE_OF_HEALTH, .reg = AGE, .bits = 16, .lsb = 1.0 / 256,
	 .min = 0.0, .max = 255.9961},
	{.chan = SENSOR_CHAN_GAUGE_CYCLE_COUNT, .reg = CYCLES, .bits = 16, .lsb = 0.01,
	 .min = 0.0, .max = 655.35},
	{.chan = SENSOR_CHAN_GAUGE_TIME_TO_EMPTY, .reg = TTE, .bits = 16, .lsb = 5.625 / 60,
	 .min = 0.0, .max = 6143.90625},
	{.chan = SENSOR_CHAN_GAUGE_TIME_TO_FULL, .reg = TTF, .bits = 16, .lsb = 5.625 / 60,
	 .min = 0.0, .max = 6143.90625},
};

/*
 * FStat.DNR (data-not-ready) is set to 1 at reset and cleared once outputs are valid (user
 * guide, FStat Register (3Dh)); polarity is inverted from the ready field's set-on-sample
 * convention, so clear it explicitly whenever any channel is sampled.
 */
static bool sample(const struct emul *target, uint8_t addr, uint32_t value)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(addr);
	ARG_UNUSED(value);
	data->regs[FSTAT] &= ~FSTAT_DNR;

	return true;
}

/*
 * Shutdown (Config.SHDN) stops all activity; measurements resume once it is cleared (user
 * guide, Modes of Operation). Injected inputs are retained across shutdown per the framework's
 * disabled condition and are re-sampled on the next fetch once SHDN reads back 0.
 */
EMUL_SENSOR_REGMAP_DEFINE(registers, channels, .reg_bytes = 2,
	.disabled = {.reg = CONFIG, .mask = CONFIG_SHDN, .value = CONFIG_SHDN},
	.sample = sample);
