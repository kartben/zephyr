/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT invensense_mpu6050

#include <zephyr/drivers/emul_sensor_regmap.h>

/* RM-MPU-6000A-00 Rev. 4.0, sections 3-4:
 * https://cdn.sparkfun.com/datasheets/Sensors/Accelerometers/RM-MPU-6000A.pdf
 */
enum {
	SMPLRT_DIV = 0x19, CONFIG = 0x1a, GYRO_CONFIG = 0x1b, ACCEL_CONFIG = 0x1c,
	FIFO_EN = 0x23, INT_PIN_CFG = 0x37, INT_ENABLE = 0x38, INT_STATUS = 0x3a,
	ACCEL_XOUT = 0x3b, ACCEL_YOUT = 0x3d, ACCEL_ZOUT = 0x3f, TEMP_OUT = 0x41,
	GYRO_XOUT = 0x43, GYRO_YOUT = 0x45, GYRO_ZOUT = 0x47,
	SIGNAL_PATH_RESET = 0x68, USER_CTRL = 0x6a, PWR_MGMT_1 = 0x6b,
	PWR_MGMT_2 = 0x6c, FIFO_COUNT = 0x72, FIFO_R_W = 0x74, WHO_AM_I = 0x75,
};

static const struct emul_sensor_reg registers[] = {
	{SMPLRT_DIV, "SMPLRT_DIV", .bytes = 1, .write_mask = 0xff},
	{CONFIG, "CONFIG", .bytes = 1, .write_mask = 0x3f},
	{GYRO_CONFIG, "GYRO_CONFIG", .bytes = 1, .write_mask = 0xf8},
	{ACCEL_CONFIG, "ACCEL_CONFIG", .bytes = 1, .write_mask = 0xf8},
	{FIFO_EN, "FIFO_EN", .bytes = 1, .write_mask = 0xff},
	{INT_PIN_CFG, "INT_PIN_CFG", .bytes = 1, .write_mask = 0xfe},
	{INT_ENABLE, "INT_ENABLE", .bytes = 1, .write_mask = 0x59},
	{INT_STATUS, "INT_STATUS", EMUL_SENSOR_REG_RO, .bytes = 1, .clear_on_read = 0x59},
	{ACCEL_XOUT, "ACCEL_XOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{ACCEL_YOUT, "ACCEL_YOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{ACCEL_ZOUT, "ACCEL_ZOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{TEMP_OUT, "TEMP_OUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{GYRO_XOUT, "GYRO_XOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{GYRO_YOUT, "GYRO_YOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{GYRO_ZOUT, "GYRO_ZOUT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{SIGNAL_PATH_RESET, "SIGNAL_PATH_RESET", .bytes = 1, .write_mask = 0x07,
	 .self_clear = 0x07},
	{USER_CTRL, "USER_CTRL", .bytes = 1, .write_mask = 0x77, .self_clear = 0x07},
	{PWR_MGMT_1, "PWR_MGMT_1", .bytes = 1, .reset = 0x40, .write_mask = 0xef,
			.reset_on_write = BIT(7)},
	{PWR_MGMT_2, "PWR_MGMT_2", .bytes = 1, .write_mask = 0xff},
	{FIFO_COUNT, "FIFO_COUNT", EMUL_SENSOR_REG_RO, .bytes = 2},
	{FIFO_R_W, "FIFO_R_W", EMUL_SENSOR_REG_RO, .bytes = 1},
	{WHO_AM_I, "WHO_AM_I", EMUL_SENSOR_REG_RO, .bytes = 1, .reset = 0x68},
};

#define ACCEL_RANGES {                                                                          \
	{.lsb = 9.80665 / 16384}, {.lsb = 9.80665 / 8192},                                      \
	{.lsb = 9.80665 / 4096}, {.lsb = 9.80665 / 2048},                                       \
}
#define GYRO_RANGES {                                                                           \
	{.lsb = (3.141592653589793 / 180) / 131},                                               \
	{.lsb = (3.141592653589793 / 180) / 65.5},                                              \
	{.lsb = (3.141592653589793 / 180) / 32.8},                                              \
	{.lsb = (3.141592653589793 / 180) / 16.4},                                              \
}

#define AXIS(type, axis, output, config_reg, scales, standby)                                   \
	{.chan = SENSOR_CHAN_##type##_##axis, .reg = output,                                    \
	 .select = {.reg = config_reg, .mask = 0x18},                                           \
	 .variants = scales, .is_signed = true, .bits = 16,                                     \
	 .disabled = {.reg = PWR_MGMT_2, .mask = BIT(standby), .value = BIT(standby)},          \
	 .ready = {.reg = INT_STATUS, .mask = BIT(0)}}

static const struct emul_sensor_channel channels[] = {
	AXIS(ACCEL, X, ACCEL_XOUT, ACCEL_CONFIG, ACCEL_RANGES, 5),
	AXIS(ACCEL, Y, ACCEL_YOUT, ACCEL_CONFIG, ACCEL_RANGES, 4),
	AXIS(ACCEL, Z, ACCEL_ZOUT, ACCEL_CONFIG, ACCEL_RANGES, 3),
	{.chan = SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT, .is_signed = true, .bits = 16,
	 .lsb = 1.0 / 340,
	 .offset = 36.53, .min = -40, .max = 85,
	 .disabled = {.reg = PWR_MGMT_1, .mask = BIT(3), .value = BIT(3)},
	 .ready = {.reg = INT_STATUS, .mask = BIT(0)}},
	AXIS(GYRO, X, GYRO_XOUT, GYRO_CONFIG, GYRO_RANGES, 2),
	AXIS(GYRO, Y, GYRO_YOUT, GYRO_CONFIG, GYRO_RANGES, 1),
	AXIS(GYRO, Z, GYRO_ZOUT, GYRO_CONFIG, GYRO_RANGES, 0),
};

static void read(const struct emul *target, uint8_t reg)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(reg);
	if ((data->regs[INT_PIN_CFG] & BIT(4)) != 0U) {
		data->regs[INT_STATUS] = 0;
	}
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_sensor_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == USER_CTRL && (data->regs[reg] & BIT(0)) != 0U) {
		for (uint8_t out = ACCEL_XOUT; out <= GYRO_ZOUT; out += 2U) {
			data->regs[out] = 0;
		}
	}
}

EMUL_SENSOR_REGMAP_DEFINE(registers, channels,
	.byte_addressed = true, .big_endian = true,
	.disabled = {.reg = PWR_MGMT_1, .mask = BIT(6), .value = BIT(6)},
	.read = read, .write = write);
