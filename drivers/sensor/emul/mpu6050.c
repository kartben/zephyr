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
	[SIGNAL_PATH_RESET] = {.bytes = 1, .write_mask = 0x07},
	[USER_CTRL] = {.bytes = 1, .write_mask = 0x77},
	[PWR_MGMT_1] = {.bytes = 1, .reset = 0x40, .write_mask = 0xef},
	[PWR_MGMT_2] = {.bytes = 1, .write_mask = 0xff},
	[FIFO_COUNT] = {.bytes = 2},
	[FIFO_R_W] = {.bytes = 1},
	[WHO_AM_I] = {.bytes = 1, .reset = 0x68},
};

static const struct emul_regmap_channel channels[] = {
	{.channel = SENSOR_CHAN_ACCEL_X, .reg = ACCEL_XOUT},
	{.channel = SENSOR_CHAN_ACCEL_Y, .reg = ACCEL_YOUT},
	{.channel = SENSOR_CHAN_ACCEL_Z, .reg = ACCEL_ZOUT},
	{.channel = SENSOR_CHAN_DIE_TEMP, .reg = TEMP_OUT, .lsb = 1.0 / 340,
	 .offset = 36.53, .min = -40, .max = 85},
	{.channel = SENSOR_CHAN_GYRO_X, .reg = GYRO_XOUT},
	{.channel = SENSOR_CHAN_GYRO_Y, .reg = GYRO_YOUT},
	{.channel = SENSOR_CHAN_GYRO_Z, .reg = GYRO_ZOUT},
};

static void channel(const struct emul *target, struct emul_regmap_channel *ch)
{
	struct emul_regmap_data *data = target->data;
	static const double gyro_sensitivity[] = {131, 65.5, 32.8, 16.4};
	uint8_t fs;

	if (ch->reg < TEMP_OUT) {
		fs = (data->values[ACCEL_CONFIG] >> 3) & 3U;
		ch->lsb = 9.80665 / (16384U >> fs);
	} else if (ch->reg > TEMP_OUT) {
		fs = (data->values[GYRO_CONFIG] >> 3) & 3U;
		ch->lsb = (3.141592653589793 / 180) / gyro_sensitivity[fs];
	} else {
		return;
	}
	ch->min = -32768 * ch->lsb;
	ch->max = 32767 * ch->lsb;
}

static bool sample(const struct emul *target, uint8_t reg, uint32_t value)
{
	struct emul_regmap_data *data = target->data;

	ARG_UNUSED(value);
	if ((data->values[PWR_MGMT_1] & BIT(6)) != 0U ||
	    (reg == TEMP_OUT && (data->values[PWR_MGMT_1] & BIT(3)) != 0U)) {
		return false;
	}
	if (reg < TEMP_OUT &&
	    (data->values[PWR_MGMT_2] & BIT(5U - (reg - ACCEL_XOUT) / 2U)) != 0U) {
		return false;
	}
	if (reg > TEMP_OUT &&
	    (data->values[PWR_MGMT_2] & BIT(2U - (reg - GYRO_XOUT) / 2U)) != 0U) {
		return false;
	}
	data->values[INT_STATUS] |= BIT(0);
	return true;
}

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
	if (reg == PWR_MGMT_1 && (data->values[reg] & BIT(7)) != 0U) {
		emul_regmap_reset(target);
		return;
	}
	if (reg == SIGNAL_PATH_RESET) {
		data->values[reg] = 0;
	}
	if (reg == USER_CTRL) {
		if ((data->values[reg] & BIT(0)) != 0U) {
			for (uint8_t out = ACCEL_XOUT; out <= GYRO_ZOUT; out += 2U) {
				data->values[out] = 0;
			}
		}
		data->values[reg] &= ~0x07U;
	}
}

static const struct emul_regmap_config config = {
	.registers = registers, .register_count = ARRAY_SIZE(registers),
	.channels = channels, .channel_count = ARRAY_SIZE(channels),
	.byte_addressed = true,
	.read = read, .write = write, .channel = channel, .sample = sample,
};

#define DEFINE(inst) EMUL_REGMAP_DT_INST_DEFINE(inst, config, registers, channels);
DT_INST_FOREACH_STATUS_OKAY(DEFINE)
