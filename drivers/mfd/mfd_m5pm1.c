/*
 * Copyright (c) 2026 The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT m5stack_m5pm1

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/m5pm1.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(mfd_m5pm1, CONFIG_MFD_LOG_LEVEL);

#define M5PM1_REG_DEVICE_ID    0x00
#define M5PM1_REG_DEVICE_MODEL 0x01

#define M5PM1_DEVICE_ID    0x50
#define M5PM1_DEVICE_MODEL 0x20

struct m5pm1_config {
	struct i2c_dt_spec i2c;
};

struct m5pm1_data {
	struct k_mutex lock;
};

int mfd_m5pm1_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct m5pm1_config *config = dev->config;
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_reg_read_byte_dt(&config->i2c, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct m5pm1_config *config = dev->config;
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_reg_write_byte_dt(&config->i2c, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	const struct m5pm1_config *config = dev->config;
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_reg_update_byte_dt(&config->i2c, reg, mask, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_burst_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct m5pm1_config *config = dev->config;
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_burst_read_dt(&config->i2c, reg, buf, len);
	k_mutex_unlock(&data->lock);

	return ret;
}

static int m5pm1_init(const struct device *dev)
{
	const struct m5pm1_config *config = dev->config;
	struct m5pm1_data *data = dev->data;
	uint8_t id;
	uint8_t model;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	ret = mfd_m5pm1_read_reg(dev, M5PM1_REG_DEVICE_ID, &id);
	if (ret < 0) {
		LOG_ERR("Failed to read M5PM1 device ID: %d", ret);
		return ret;
	}

	ret = mfd_m5pm1_read_reg(dev, M5PM1_REG_DEVICE_MODEL, &model);
	if (ret < 0) {
		LOG_ERR("Failed to read M5PM1 device model: %d", ret);
		return ret;
	}

	if (id != M5PM1_DEVICE_ID || model != M5PM1_DEVICE_MODEL) {
		LOG_ERR("Unexpected M5PM1 ID/model 0x%02x/0x%02x", id, model);
		return -ENODEV;
	}

	return 0;
}

#define M5PM1_DEFINE(inst)                                                                         \
	static const struct m5pm1_config m5pm1_config_##inst = {                                   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
	static struct m5pm1_data m5pm1_data_##inst;                                                \
	DEVICE_DT_INST_DEFINE(inst, m5pm1_init, NULL, &m5pm1_data_##inst, &m5pm1_config_##inst,    \
			      POST_KERNEL, CONFIG_MFD_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(M5PM1_DEFINE)
