/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT invensense_mpu9250

#include <zephyr/drivers/emul_sensor_regmap.h>

#define G 9.80665
#define PI 3.14159265358979323846
#define D2R (PI / 180.0)

/*
 * TDK InvenSense MPU-9250 Register Map and Descriptions, Document Number RM-MPU-9250A-00,
 * Revision 1.6, Release Date 01/07/2015, section 3 (register map) and section 4 (register
 * descriptions): https://invensense.tdk.com/wp-content/uploads/2017/11/RM-MPU-9250A-00-v1.6.pdf
 *
 * Sensitivity scale factors, the temperature sensor formula constants, and the WHO_AM_I value
 * are cross-checked against the MPU-9250 Product Specification, Document Number
 * PS-MPU-9250A-01, Revision 1.0, Release Date 01/17/2014, sections 3.1, 3.2 and 3.4.2.
 *
 * The magnetometer (AK8963) is a second physical die reached through the auxiliary I2C master
 * or bypass mode (section 5 of the register map document); it is not a field of this device's
 * own register map and is not modeled here (see unsupported list below the register table).
 */
enum {
	SELF_TEST_X_GYRO = 0x00,
	SELF_TEST_Y_GYRO = 0x01,
	SELF_TEST_Z_GYRO = 0x02,
	SELF_TEST_X_ACCEL = 0x0D,
	SELF_TEST_Y_ACCEL = 0x0E,
	SELF_TEST_Z_ACCEL = 0x0F,
	XG_OFFSET_H = 0x13,
	XG_OFFSET_L = 0x14,
	YG_OFFSET_H = 0x15,
	YG_OFFSET_L = 0x16,
	ZG_OFFSET_H = 0x17,
	ZG_OFFSET_L = 0x18,
	SMPLRT_DIV = 0x19,
	CONFIG = 0x1A,
	GYRO_CONFIG = 0x1B,
	ACCEL_CONFIG = 0x1C,
	ACCEL_CONFIG_2 = 0x1D,
	LP_ACCEL_ODR = 0x1E,
	WOM_THR = 0x1F,
	FIFO_EN = 0x23,
	I2C_MST_CTRL = 0x24,
	I2C_SLV0_ADDR = 0x25,
	I2C_SLV0_REG = 0x26,
	I2C_SLV0_CTRL = 0x27,
	I2C_SLV1_ADDR = 0x28,
	I2C_SLV1_REG = 0x29,
	I2C_SLV1_CTRL = 0x2A,
	I2C_SLV2_ADDR = 0x2B,
	I2C_SLV2_REG = 0x2C,
	I2C_SLV2_CTRL = 0x2D,
	I2C_SLV3_ADDR = 0x2E,
	I2C_SLV3_REG = 0x2F,
	I2C_SLV3_CTRL = 0x30,
	I2C_SLV4_ADDR = 0x31,
	I2C_SLV4_REG = 0x32,
	I2C_SLV4_DO = 0x33,
	I2C_SLV4_CTRL = 0x34,
	I2C_SLV4_DI = 0x35,
	I2C_MST_STATUS = 0x36,
	INT_PIN_CFG = 0x37,
	INT_ENABLE = 0x38,
	INT_STATUS = 0x3A,
	ACCEL_XOUT_H = 0x3B,
	ACCEL_XOUT_L = 0x3C,
	ACCEL_YOUT_H = 0x3D,
	ACCEL_YOUT_L = 0x3E,
	ACCEL_ZOUT_H = 0x3F,
	ACCEL_ZOUT_L = 0x40,
	TEMP_OUT_H = 0x41,
	TEMP_OUT_L = 0x42,
	GYRO_XOUT_H = 0x43,
	GYRO_XOUT_L = 0x44,
	GYRO_YOUT_H = 0x45,
	GYRO_YOUT_L = 0x46,
	GYRO_ZOUT_H = 0x47,
	GYRO_ZOUT_L = 0x48,
	EXT_SENS_DATA_00 = 0x49,
	/* EXT_SENS_DATA_01 to _23 follow at 0x4A to 0x60 */
	I2C_SLV0_DO = 0x63,
	I2C_SLV1_DO = 0x64,
	I2C_SLV2_DO = 0x65,
	I2C_SLV3_DO = 0x66,
	I2C_MST_DELAY_CTRL = 0x67,
	SIGNAL_PATH_RESET = 0x68,
	MOT_DETECT_CTRL = 0x69,
	USER_CTRL = 0x6A,
	PWR_MGMT_1 = 0x6B,
	PWR_MGMT_2 = 0x6C,
	FIFO_COUNTH = 0x72,
	FIFO_COUNTL = 0x73,
	FIFO_R_W = 0x74,
	WHO_AM_I = 0x75,
	XA_OFFSET_H = 0x77,
	XA_OFFSET_L = 0x78,
	YA_OFFSET_H = 0x7A,
	YA_OFFSET_L = 0x7B,
	ZA_OFFSET_H = 0x7D,
	ZA_OFFSET_L = 0x7E,
};

/* PWR_MGMT_2 per-axis disable bits (register 108, section 4.35). */
#define DISABLE_XA BIT(5)
#define DISABLE_YA BIT(4)
#define DISABLE_ZA BIT(3)
#define DISABLE_XG BIT(2)
#define DISABLE_YG BIT(1)
#define DISABLE_ZG BIT(0)

/* PWR_MGMT_1 bits (register 107, section 4.34). */
#define H_RESET BIT(7)
#define SLEEP BIT(6)
#define PD_PTAT BIT(3)

/* INT_STATUS/INT_ENABLE documented bits (registers 56 and 58, sections 4.20-4.21). */
#define RAW_DATA_RDY_INT BIT(0)
#define FSYNC_INT BIT(3)
#define FIFO_OFLOW_INT BIT(4)
#define WOM_INT BIT(6)
#define INT_STATUS_BITS (WOM_INT | FIFO_OFLOW_INT | FSYNC_INT | RAW_DATA_RDY_INT)

/*
 * Register address byte is 7 bits wide; 0x7E is the highest implemented address. Reserved
 * addresses are not documented, so treat them as read-only storage of unknown content.
 */
#define RESERVED(addr) {addr, "reserved", EMUL_SENSOR_REG_RO}

static const struct emul_sensor_reg regs[] = {
	{SELF_TEST_X_GYRO, "SELF_TEST_X_GYRO", .write_mask = 0xFF},
	{SELF_TEST_Y_GYRO, "SELF_TEST_Y_GYRO", .write_mask = 0xFF},
	{SELF_TEST_Z_GYRO, "SELF_TEST_Z_GYRO", .write_mask = 0xFF},
	RESERVED(0x03), RESERVED(0x04), RESERVED(0x05), RESERVED(0x06), RESERVED(0x07),
	RESERVED(0x08), RESERVED(0x09), RESERVED(0x0A), RESERVED(0x0B), RESERVED(0x0C),
	{SELF_TEST_X_ACCEL, "SELF_TEST_X_ACCEL", .write_mask = 0xFF},
	{SELF_TEST_Y_ACCEL, "SELF_TEST_Y_ACCEL", .write_mask = 0xFF},
	{SELF_TEST_Z_ACCEL, "SELF_TEST_Z_ACCEL", .write_mask = 0xFF},
	RESERVED(0x10), RESERVED(0x11), RESERVED(0x12),
	{XG_OFFSET_H, "XG_OFFSET_H", .write_mask = 0xFF},
	{XG_OFFSET_L, "XG_OFFSET_L", .write_mask = 0xFF},
	{YG_OFFSET_H, "YG_OFFSET_H", .write_mask = 0xFF},
	{YG_OFFSET_L, "YG_OFFSET_L", .write_mask = 0xFF},
	{ZG_OFFSET_H, "ZG_OFFSET_H", .write_mask = 0xFF},
	{ZG_OFFSET_L, "ZG_OFFSET_L", .write_mask = 0xFF},
	{SMPLRT_DIV, "SMPLRT_DIV", .write_mask = 0xFF},
	/* Bit 7 reserved. */
	{CONFIG, "CONFIG", .write_mask = 0x7F},
	/* Bit 2 reserved. */
	{GYRO_CONFIG, "GYRO_CONFIG", .write_mask = 0xFB},
	/* Bits 2:0 reserved. */
	{ACCEL_CONFIG, "ACCEL_CONFIG", .write_mask = 0xF8},
	/* Bits 7:4 reserved. */
	{ACCEL_CONFIG_2, "ACCEL_CONFIG_2", .write_mask = 0x0F},
	/* Bits 7:4 reserved. */
	{LP_ACCEL_ODR, "LP_ACCEL_ODR", .write_mask = 0x0F},
	{WOM_THR, "WOM_THR", .write_mask = 0xFF},
	RESERVED(0x20), RESERVED(0x21), RESERVED(0x22),
	{FIFO_EN, "FIFO_EN", .write_mask = 0xFF},
	{I2C_MST_CTRL, "I2C_MST_CTRL", .write_mask = 0xFF},
	{I2C_SLV0_ADDR, "I2C_SLV0_ADDR", .write_mask = 0xFF},
	{I2C_SLV0_REG, "I2C_SLV0_REG", .write_mask = 0xFF},
	{I2C_SLV0_CTRL, "I2C_SLV0_CTRL", .write_mask = 0xFF},
	{I2C_SLV1_ADDR, "I2C_SLV1_ADDR", .write_mask = 0xFF},
	{I2C_SLV1_REG, "I2C_SLV1_REG", .write_mask = 0xFF},
	{I2C_SLV1_CTRL, "I2C_SLV1_CTRL", .write_mask = 0xFF},
	{I2C_SLV2_ADDR, "I2C_SLV2_ADDR", .write_mask = 0xFF},
	{I2C_SLV2_REG, "I2C_SLV2_REG", .write_mask = 0xFF},
	{I2C_SLV2_CTRL, "I2C_SLV2_CTRL", .write_mask = 0xFF},
	{I2C_SLV3_ADDR, "I2C_SLV3_ADDR", .write_mask = 0xFF},
	{I2C_SLV3_REG, "I2C_SLV3_REG", .write_mask = 0xFF},
	{I2C_SLV3_CTRL, "I2C_SLV3_CTRL", .write_mask = 0xFF},
	{I2C_SLV4_ADDR, "I2C_SLV4_ADDR", .write_mask = 0xFF},
	{I2C_SLV4_REG, "I2C_SLV4_REG", .write_mask = 0xFF},
	{I2C_SLV4_DO, "I2C_SLV4_DO", .write_mask = 0xFF},
	{I2C_SLV4_CTRL, "I2C_SLV4_CTRL", .write_mask = 0xFF},
	{I2C_SLV4_DI, "I2C_SLV4_DI", EMUL_SENSOR_REG_RO},
	{I2C_MST_STATUS, "I2C_MST_STATUS", EMUL_SENSOR_REG_RO},
	/* Bit 0 reserved. */
	{INT_PIN_CFG, "INT_PIN_CFG", .write_mask = 0xFE},
	{INT_ENABLE, "INT_ENABLE", .write_mask = INT_STATUS_BITS},
	RESERVED(0x39),
	{INT_STATUS, "INT_STATUS", EMUL_SENSOR_REG_RO, .clear_on_read = INT_STATUS_BITS},
	{ACCEL_XOUT_H, "ACCEL_XOUT_H", EMUL_SENSOR_REG_RO},
	{ACCEL_XOUT_L, "ACCEL_XOUT_L", EMUL_SENSOR_REG_RO},
	{ACCEL_YOUT_H, "ACCEL_YOUT_H", EMUL_SENSOR_REG_RO},
	{ACCEL_YOUT_L, "ACCEL_YOUT_L", EMUL_SENSOR_REG_RO},
	{ACCEL_ZOUT_H, "ACCEL_ZOUT_H", EMUL_SENSOR_REG_RO},
	{ACCEL_ZOUT_L, "ACCEL_ZOUT_L", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_H, "TEMP_OUT_H", EMUL_SENSOR_REG_RO},
	{TEMP_OUT_L, "TEMP_OUT_L", EMUL_SENSOR_REG_RO},
	{GYRO_XOUT_H, "GYRO_XOUT_H", EMUL_SENSOR_REG_RO},
	{GYRO_XOUT_L, "GYRO_XOUT_L", EMUL_SENSOR_REG_RO},
	{GYRO_YOUT_H, "GYRO_YOUT_H", EMUL_SENSOR_REG_RO},
	{GYRO_YOUT_L, "GYRO_YOUT_L", EMUL_SENSOR_REG_RO},
	{GYRO_ZOUT_H, "GYRO_ZOUT_H", EMUL_SENSOR_REG_RO},
	{GYRO_ZOUT_L, "GYRO_ZOUT_L", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 0, "EXT_SENS_DATA_00", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 1, "EXT_SENS_DATA_01", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 2, "EXT_SENS_DATA_02", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 3, "EXT_SENS_DATA_03", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 4, "EXT_SENS_DATA_04", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 5, "EXT_SENS_DATA_05", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 6, "EXT_SENS_DATA_06", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 7, "EXT_SENS_DATA_07", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 8, "EXT_SENS_DATA_08", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 9, "EXT_SENS_DATA_09", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 10, "EXT_SENS_DATA_10", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 11, "EXT_SENS_DATA_11", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 12, "EXT_SENS_DATA_12", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 13, "EXT_SENS_DATA_13", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 14, "EXT_SENS_DATA_14", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 15, "EXT_SENS_DATA_15", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 16, "EXT_SENS_DATA_16", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 17, "EXT_SENS_DATA_17", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 18, "EXT_SENS_DATA_18", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 19, "EXT_SENS_DATA_19", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 20, "EXT_SENS_DATA_20", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 21, "EXT_SENS_DATA_21", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 22, "EXT_SENS_DATA_22", EMUL_SENSOR_REG_RO},
	{EXT_SENS_DATA_00 + 23, "EXT_SENS_DATA_23", EMUL_SENSOR_REG_RO},
	RESERVED(0x61), RESERVED(0x62),
	{I2C_SLV0_DO, "I2C_SLV0_DO", .write_mask = 0xFF},
	{I2C_SLV1_DO, "I2C_SLV1_DO", .write_mask = 0xFF},
	{I2C_SLV2_DO, "I2C_SLV2_DO", .write_mask = 0xFF},
	{I2C_SLV3_DO, "I2C_SLV3_DO", .write_mask = 0xFF},
	/* Bits 6:5 reserved. */
	{I2C_MST_DELAY_CTRL, "I2C_MST_DELAY_CTRL", .write_mask = 0x9F},
	/* Bits 7:3 reserved; GYRO_RST, ACCEL_RST and TEMP_RST are reset-path pulses like
	 * USER_CTRL's FIFO_RST/I2C_MST_RST/SIG_COND_RST, which the datasheet documents as
	 * auto-clearing (section 4.33); this register's own text does not repeat that
	 * statement, so treat it the same way.
	 */
	{SIGNAL_PATH_RESET, "SIGNAL_PATH_RESET", .write_mask = 0x07, .self_clear = 0x07},
	/* Bits 5:0 reserved. */
	{MOT_DETECT_CTRL, "MOT_DETECT_CTRL", .write_mask = 0xC0},
	/* Bits 7 and 3 reserved; FIFO_RST, I2C_MST_RST and SIG_COND_RST auto-clear. */
	{USER_CTRL, "USER_CTRL", .write_mask = 0x77, .self_clear = 0x07},
	/* Reset value 0x01: CLKSEL = 1 (auto-select PLL), SLEEP = 0 (section 4.34 note). */
	{PWR_MGMT_1, "PWR_MGMT_1", .reset = 0x01, .write_mask = 0xFF, .self_clear = H_RESET,
	 .reset_on_write = H_RESET},
	/* Bits 7:6 reserved. */
	{PWR_MGMT_2, "PWR_MGMT_2", .write_mask = 0x3F},
	RESERVED(0x6D), RESERVED(0x6E), RESERVED(0x6F), RESERVED(0x70), RESERVED(0x71),
	{FIFO_COUNTH, "FIFO_COUNTH", .write_mask = 0xFF},
	{FIFO_COUNTL, "FIFO_COUNTL", .write_mask = 0xFF},
	{FIFO_R_W, "FIFO_R_W", .write_mask = 0xFF},
	/* Table 1's exception list gives the reset value as 0x71, matching the prose in
	 * section 4.38 ("The default value of the register is 0x71"); the register's own
	 * detail box in that section prints a stale 0x68 left over from the MPU-60X0
	 * template.
	 */
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .reset = 0x71},
	RESERVED(0x76),
	{XA_OFFSET_H, "XA_OFFSET_H", .write_mask = 0xFF},
	/* Bit 0 reserved. */
	{XA_OFFSET_L, "XA_OFFSET_L", .write_mask = 0xFE},
	RESERVED(0x79),
	{YA_OFFSET_H, "YA_OFFSET_H", .write_mask = 0xFF},
	{YA_OFFSET_L, "YA_OFFSET_L", .write_mask = 0xFE},
	RESERVED(0x7C),
	{ZA_OFFSET_H, "ZA_OFFSET_H", .write_mask = 0xFF},
	{ZA_OFFSET_L, "ZA_OFFSET_L", .write_mask = 0xFE},
};

/*
 * Accelerometer output, 16-bit two's complement, MSB register first (ACCEL_xOUT_H then
 * ACCEL_xOUT_L). ACCEL_FS_SEL<1:0> in ACCEL_CONFIG bits 4:3 selects +/-2, 4, 8 or 16 g at
 * 16384, 8192, 4096 or 2048 LSB/g (product spec table 2, section 3.2). PWR_MGMT_2 DIS_xA
 * disables that axis's accelerometer (section 4.35).
 */
#define ACCEL(_chan, _reg, _dis)                                                                \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                     \
	 .select = {ACCEL_CONFIG, GENMASK(4, 3)},                                               \
	 .variants = {{.lsb = 2.0 * G / 32768, .min = -2.0 * G, .max = 2.0 * G},                 \
		      {.lsb = 4.0 * G / 32768, .min = -4.0 * G, .max = 4.0 * G},                 \
		      {.lsb = 8.0 * G / 32768, .min = -8.0 * G, .max = 8.0 * G},                 \
		      {.lsb = 16.0 * G / 32768, .min = -16.0 * G, .max = 16.0 * G}},             \
	 .ready = {INT_STATUS, RAW_DATA_RDY_INT}, .disabled = {PWR_MGMT_2, _dis, _dis}}

/*
 * Gyroscope output, 16-bit two's complement, MSB register first. GYRO_FS_SEL<1:0> in
 * GYRO_CONFIG bits 4:3 selects +/-250, 500, 1000 or 2000 dps at the documented (non-integer)
 * sensitivity of 131, 65.5, 32.8 or 16.4 LSB/(deg/s) (product spec table 1, section 3.1).
 * PWR_MGMT_2 DIS_xG disables that axis's gyroscope (section 4.35).
 */
#define GYRO(_chan, _reg, _dis)                                                                 \
	{_chan, .reg = _reg, .is_signed = true, .bits = 16,                                     \
	 .select = {GYRO_CONFIG, GENMASK(4, 3)},                                                \
	 .variants = {{.lsb = D2R / 131.0, .min = -250.0 * D2R, .max = 250.0 * D2R},             \
		      {.lsb = D2R / 65.5, .min = -500.0 * D2R, .max = 500.0 * D2R},              \
		      {.lsb = D2R / 32.8, .min = -1000.0 * D2R, .max = 1000.0 * D2R},            \
		      {.lsb = D2R / 16.4, .min = -2000.0 * D2R, .max = 2000.0 * D2R}},           \
	 .ready = {INT_STATUS, RAW_DATA_RDY_INT}, .disabled = {PWR_MGMT_2, _dis, _dis}}

static const struct emul_sensor_channel channels[] = {
	ACCEL(SENSOR_CHAN_ACCEL_X, ACCEL_XOUT_H, DISABLE_XA),
	ACCEL(SENSOR_CHAN_ACCEL_Y, ACCEL_YOUT_H, DISABLE_YA),
	ACCEL(SENSOR_CHAN_ACCEL_Z, ACCEL_ZOUT_H, DISABLE_ZA),
	GYRO(SENSOR_CHAN_GYRO_X, GYRO_XOUT_H, DISABLE_XG),
	GYRO(SENSOR_CHAN_GYRO_Y, GYRO_YOUT_H, DISABLE_YG),
	GYRO(SENSOR_CHAN_GYRO_Z, GYRO_ZOUT_H, DISABLE_ZG),
	/*
	 * TEMP_degC = ((TEMP_OUT - RoomTemp_Offset) / Temp_Sensitivity) + 21degC (register map
	 * section 4.23), with Temp_Sensitivity = 333.87 LSB/degC and RoomTemp_Offset = 0 LSB
	 * (product spec table "A.C. Electrical Characteristics", section 3.4.2). Operating
	 * range -40 to +85 degC (same table).
	 */
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT_H, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 333.87, .offset = 21.0, .min = -40.0, .max = 85.0,
	 .ready = {INT_STATUS, RAW_DATA_RDY_INT},
	 .disabled = {PWR_MGMT_1, PD_PTAT, PD_PTAT}},
};

/*
 * The device converts continuously while awake; clearing SLEEP does not itself go through
 * convert_on_write, so re-arm a conversion on the 1->0 edge (and on any PWR_MGMT_2 axis
 * re-enable) to avoid serving a stale sample from before shutdown. H_RESET restores the
 * reset values of every register while retaining injected inputs (section 4.34: "Reset the
 * internal registers and restores the default settings"); USER_CTRL.SIG_COND_RST additionally
 * clears the accelerometer, gyroscope and temperature data registers (section 4.33) without
 * touching their configuration.
 */
static void write(const struct emul *target, uint8_t addr, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	if (addr == PWR_MGMT_1) {
		if ((data->regs[PWR_MGMT_1] & H_RESET) != 0U) {
			emul_sensor_regmap_reset(target);
		} else if ((old & SLEEP) != 0U && (data->regs[PWR_MGMT_1] & SLEEP) == 0U) {
			emul_sensor_regmap_convert(target);
		}
	} else if (addr == PWR_MGMT_2) {
		if ((old & ~data->regs[PWR_MGMT_2] & GENMASK(5, 0)) != 0U) {
			emul_sensor_regmap_convert(target);
		}
	} else if (addr == USER_CTRL) {
		if ((data->regs[USER_CTRL] & BIT(0)) != 0U) {
			static const uint8_t data_regs[] = {
				ACCEL_XOUT_H, ACCEL_XOUT_L, ACCEL_YOUT_H, ACCEL_YOUT_L,
				ACCEL_ZOUT_H, ACCEL_ZOUT_L, TEMP_OUT_H, TEMP_OUT_L,
				GYRO_XOUT_H, GYRO_XOUT_L, GYRO_YOUT_H, GYRO_YOUT_L,
				GYRO_ZOUT_H, GYRO_ZOUT_L,
			};

			for (size_t i = 0; i < ARRAY_SIZE(data_regs); i++) {
				data->regs[data_regs[i]] = 0;
			}
		}
	}
}

EMUL_SENSOR_REGMAP_DEFINE(regs, channels, .big_endian = true,
	.disabled = {.reg = PWR_MGMT_1, .mask = SLEEP, .value = SLEEP}, .write = write);
