/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT feetech_scs0009

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/misc/feetech_scs/feetech_scs.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include "feetech_scs_bus.h"

LOG_MODULE_REGISTER(feetech_scs_servo, CONFIG_FEETECH_SCS_LOG_LEVEL);

/* SCSCL series register map, multi-byte values are big-endian */
#define SCS_REG_ANGLE_LIMITS     9  /* min (2), max (2) */
#define SCS_REG_TORQUE_ENABLE    40
#define SCS_REG_GOAL_POSITION    42 /* position (2), time (2), speed (2) */
#define SCS_REG_GOAL_TIME        44 /* PWM duty cycle in PWM mode */
#define SCS_REG_PRESENT_POSITION 56
#define SCS_REG_PRESENT_CURRENT  69

#define SCS_ANGLE_LIMITS_LEN 4U
#define SCS_STATUS_LEN       (SCS_REG_PRESENT_CURRENT + 2U - SCS_REG_PRESENT_POSITION)

/* Offsets in the status block read from SCS_REG_PRESENT_POSITION */
#define SCS_STATUS_POSITION    0
#define SCS_STATUS_SPEED       2
#define SCS_STATUS_LOAD        4
#define SCS_STATUS_VOLTAGE     6
#define SCS_STATUS_TEMPERATURE 7
#define SCS_STATUS_MOVING      10
#define SCS_STATUS_CURRENT     13

/* Signed values are sent as a magnitude and a direction bit */
#define SCS_SPEED_SIGN_BIT   15
#define SCS_LOAD_SIGN_BIT    10
#define SCS_CURRENT_SIGN_BIT 15
#define SCS_PWM_SIGN_BIT     10

#define SCS_ID_MAX 253

struct feetech_scs_servo_config {
	const struct device *bus;
	uint8_t id;
};

struct feetech_scs_servo_data {
	struct k_mutex lock;
	enum feetech_scs_mode mode;
	uint8_t angle_limits[SCS_ANGLE_LIMITS_LEN];
};

static int feetech_scs_write(const struct device *dev, uint8_t reg, const uint8_t *val,
			     size_t len)
{
	const struct feetech_scs_servo_config *config = dev->config;
	uint8_t params[FEETECH_SCS_MAX_PARAMS];

	if (len >= sizeof(params)) {
		return -EINVAL;
	}

	params[0] = reg;
	memcpy(&params[1], val, len);

	return feetech_scs_bus_transceive(config->bus, config->id, FEETECH_SCS_INST_WRITE, params,
					  len + 1U, NULL, 0U);
}

static int feetech_scs_read(const struct device *dev, uint8_t reg, uint8_t *val, size_t len)
{
	const struct feetech_scs_servo_config *config = dev->config;
	const uint8_t params[] = {reg, (uint8_t)len};

	return feetech_scs_bus_transceive(config->bus, config->id, FEETECH_SCS_INST_READ, params,
					  sizeof(params), val, len);
}

static int16_t feetech_scs_get_signed(const uint8_t *buf, uint8_t sign_bit)
{
	uint16_t raw = sys_get_be16(buf);
	int16_t magnitude = (int16_t)(raw & BIT_MASK(sign_bit));

	return (raw & BIT(sign_bit)) != 0U ? -magnitude : magnitude;
}

int feetech_scs_ping(const struct device *dev)
{
	const struct feetech_scs_servo_config *config = dev->config;

	return feetech_scs_bus_transceive(config->bus, config->id, FEETECH_SCS_INST_PING, NULL, 0U,
					  NULL, 0U);
}

int feetech_scs_set_torque(const struct device *dev, bool enable)
{
	uint8_t val = enable ? 1U : 0U;

	return feetech_scs_write(dev, SCS_REG_TORQUE_ENABLE, &val, sizeof(val));
}

int feetech_scs_set_position(const struct device *dev, uint16_t position, uint16_t time_ms,
			     uint16_t speed)
{
	struct feetech_scs_servo_data *data = dev->data;
	uint8_t buf[6];
	int ret;

	if (position > FEETECH_SCS_POSITION_MAX) {
		return -EINVAL;
	}

	sys_put_be16(position, &buf[0]);
	sys_put_be16(time_ms, &buf[2]);
	sys_put_be16(speed, &buf[4]);

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->mode != FEETECH_SCS_MODE_POSITION) {
		ret = -EPERM;
	} else {
		ret = feetech_scs_write(dev, SCS_REG_GOAL_POSITION, buf, sizeof(buf));
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

int feetech_scs_get_position(const struct device *dev, uint16_t *position)
{
	uint8_t buf[2];
	int ret;

	ret = feetech_scs_read(dev, SCS_REG_PRESENT_POSITION, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	*position = sys_get_be16(buf);

	return 0;
}

int feetech_scs_get_status(const struct device *dev, struct feetech_scs_status *status)
{
	uint8_t buf[SCS_STATUS_LEN];
	int ret;

	ret = feetech_scs_read(dev, SCS_REG_PRESENT_POSITION, buf, sizeof(buf));
	if (ret < 0) {
		return ret;
	}

	status->position = sys_get_be16(&buf[SCS_STATUS_POSITION]);
	status->speed = feetech_scs_get_signed(&buf[SCS_STATUS_SPEED], SCS_SPEED_SIGN_BIT);
	status->load = feetech_scs_get_signed(&buf[SCS_STATUS_LOAD], SCS_LOAD_SIGN_BIT);
	status->current = feetech_scs_get_signed(&buf[SCS_STATUS_CURRENT], SCS_CURRENT_SIGN_BIT);
	status->voltage = buf[SCS_STATUS_VOLTAGE];
	status->temperature = buf[SCS_STATUS_TEMPERATURE];
	status->moving = buf[SCS_STATUS_MOVING] != 0U;

	return 0;
}

int feetech_scs_set_mode(const struct device *dev, enum feetech_scs_mode mode)
{
	struct feetech_scs_servo_data *data = dev->data;
	static const uint8_t no_limits[SCS_ANGLE_LIMITS_LEN];
	int ret;

	if (mode != FEETECH_SCS_MODE_POSITION && mode != FEETECH_SCS_MODE_PWM) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (mode == data->mode) {
		ret = 0;
	} else if (mode == FEETECH_SCS_MODE_PWM) {
		/* Zero angle limits select PWM mode, keep the current ones to restore later */
		ret = feetech_scs_read(dev, SCS_REG_ANGLE_LIMITS, data->angle_limits,
				       sizeof(data->angle_limits));
		if (ret == 0) {
			ret = feetech_scs_write(dev, SCS_REG_ANGLE_LIMITS, no_limits,
						sizeof(no_limits));
		}
	} else {
		ret = feetech_scs_write(dev, SCS_REG_ANGLE_LIMITS, data->angle_limits,
					sizeof(data->angle_limits));
	}

	if (ret == 0) {
		data->mode = mode;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

int feetech_scs_set_pwm(const struct device *dev, int16_t duty)
{
	struct feetech_scs_servo_data *data = dev->data;
	uint16_t val;
	uint8_t buf[2];
	int ret;

	if (duty < -FEETECH_SCS_PWM_MAX || duty > FEETECH_SCS_PWM_MAX) {
		return -EINVAL;
	}

	val = duty < 0 ? (uint16_t)-duty | BIT(SCS_PWM_SIGN_BIT) : (uint16_t)duty;
	sys_put_be16(val, buf);

	k_mutex_lock(&data->lock, K_FOREVER);
	if (data->mode != FEETECH_SCS_MODE_PWM) {
		ret = -EPERM;
	} else {
		ret = feetech_scs_write(dev, SCS_REG_GOAL_TIME, buf, sizeof(buf));
	}
	k_mutex_unlock(&data->lock);

	return ret;
}

static int feetech_scs_servo_init(const struct device *dev)
{
	const struct feetech_scs_servo_config *config = dev->config;
	struct feetech_scs_servo_data *data = dev->data;

	if (!device_is_ready(config->bus)) {
		LOG_ERR_DEVICE_NOT_READY(config->bus);
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	data->mode = FEETECH_SCS_MODE_POSITION;

	return 0;
}

#define FEETECH_SCS_SERVO_DEFINE(inst)                                                             \
	BUILD_ASSERT(DT_INST_REG_ADDR(inst) <= SCS_ID_MAX, "Invalid Feetech SCS servo ID");        \
	static const struct feetech_scs_servo_config feetech_scs_servo_config_##inst = {           \
		.bus = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
		.id = DT_INST_REG_ADDR(inst),                                                      \
	};                                                                                         \
	static struct feetech_scs_servo_data feetech_scs_servo_data_##inst;                        \
	DEVICE_DT_INST_DEFINE(inst, feetech_scs_servo_init, NULL, &feetech_scs_servo_data_##inst,  \
			      &feetech_scs_servo_config_##inst, POST_KERNEL,                       \
			      CONFIG_FEETECH_SCS_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(FEETECH_SCS_SERVO_DEFINE)
