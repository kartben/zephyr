/* SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */
#define DT_DRV_COMPAT invensense_mpu6050

#include "regmap.h"

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

static const struct emul_regmap_register registers[] = {
	[SMPLRT_DIV] = {.bytes = 1, .write_mask = 0xff},
	[CONFIG] = {.bytes = 1, .write_mask = 0x3f},
	[GYRO_CONFIG] = {.bytes = 1, .write_mask = 0xf8},
	[ACCEL_CONFIG] = {.bytes = 1, .write_mask = 0xf8},
	[FIFO_EN] = {.bytes = 1, .write_mask = 0xff},
	[INT_PIN_CFG] = {.bytes = 1, .write_mask = 0xfe},
	[INT_ENABLE] = {.bytes = 1, .write_mask = 0x59},
	[INT_STATUS] = {.bytes = 1, .clear_on_read = 0x59},
	[ACCEL_XOUT] = {.bytes = 2},
	[ACCEL_YOUT] = {.bytes = 2},
	[ACCEL_ZOUT] = {.bytes = 2},
	[TEMP_OUT] = {.bytes = 2},
	[GYRO_XOUT] = {.bytes = 2},
	[GYRO_YOUT] = {.bytes = 2},
	[GYRO_ZOUT] = {.bytes = 2},
	[SIGNAL_PATH_RESET] = {.bytes = 1, .write_mask = 0x07, .self_clear = 0x07},
	[USER_CTRL] = {.bytes = 1, .write_mask = 0x77, .self_clear = 0x07},
	[PWR_MGMT_1] = {.bytes = 1, .reset = 0x40, .write_mask = 0xef,
			.reset_on_write = BIT(7)},
	[PWR_MGMT_2] = {.bytes = 1, .write_mask = 0xff},
	[FIFO_COUNT] = {.bytes = 2},
	[FIFO_R_W] = {.bytes = 1},
	[WHO_AM_I] = {.bytes = 1, .reset = 0x68},
};

static const struct emul_regmap_range accel_ranges[] = {
	{.lsb = 9.80665 / 16384}, {.lsb = 9.80665 / 8192},
	{.lsb = 9.80665 / 4096}, {.lsb = 9.80665 / 2048},
};

static const struct emul_regmap_range gyro_ranges[] = {
	{.lsb = (3.141592653589793 / 180) / 131},
	{.lsb = (3.141592653589793 / 180) / 65.5},
	{.lsb = (3.141592653589793 / 180) / 32.8},
	{.lsb = (3.141592653589793 / 180) / 16.4},
};

#define AXIS(type, axis, output, config_reg, scales, standby)                         \
	{.channel = SENSOR_CHAN_##type##_##axis, .reg = output,                        \
	 .range_select = {.reg = config_reg, .mask = 0x18},                           \
	 .ranges = scales, .range_count = ARRAY_SIZE(scales), .range_limits = true,   \
	 .disabled = {.reg = PWR_MGMT_2, .mask = BIT(standby), .value = BIT(standby)}, \
	 .ready = {.reg = INT_STATUS, .mask = BIT(0)}}

static const struct emul_regmap_channel channels[] = {
	AXIS(ACCEL, X, ACCEL_XOUT, ACCEL_CONFIG, accel_ranges, 5),
	AXIS(ACCEL, Y, ACCEL_YOUT, ACCEL_CONFIG, accel_ranges, 4),
	AXIS(ACCEL, Z, ACCEL_ZOUT, ACCEL_CONFIG, accel_ranges, 3),
	{.channel = SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT, .lsb = 1.0 / 340,
	 .offset = 36.53, .min = -40, .max = 85,
	 .disabled = {.reg = PWR_MGMT_1, .mask = BIT(3), .value = BIT(3)},
	 .ready = {.reg = INT_STATUS, .mask = BIT(0)}},
	AXIS(GYRO, X, GYRO_XOUT, GYRO_CONFIG, gyro_ranges, 2),
	AXIS(GYRO, Y, GYRO_YOUT, GYRO_CONFIG, gyro_ranges, 1),
	AXIS(GYRO, Z, GYRO_ZOUT, GYRO_CONFIG, gyro_ranges, 0),
};

static void read(const struct emul *target, uint8_t reg)
{
	struct emul_regmap_data *data = target->data;

	ARG_UNUSED(reg);
	if ((data->values[INT_PIN_CFG] & BIT(4)) != 0U) {
		data->values[INT_STATUS] = 0;
	}
}

static void write(const struct emul *target, uint8_t reg, uint32_t old)
{
	struct emul_regmap_data *data = target->data;

	ARG_UNUSED(old);
	if (reg == USER_CTRL && (data->values[reg] & BIT(0)) != 0U) {
		for (uint8_t out = ACCEL_XOUT; out <= GYRO_ZOUT; out += 2U) {
			data->values[out] = 0;
		}
	}
}

EMUL_REGMAP_MODEL(registers, channels,
	.byte_addressed = true,
	.disabled = {.reg = PWR_MGMT_1, .mask = BIT(6), .value = BIT(6)},
	.read = read, .write = write);
