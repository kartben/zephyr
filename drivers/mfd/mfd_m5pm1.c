/*
 * Copyright (c) 2026 Benjamin Cabé <benjamin@zephyrproject.org>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT m5stack_m5pm1

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/m5pm1.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(mfd_m5pm1, CONFIG_MFD_LOG_LEVEL);

struct mfd_m5pm1_config {
	struct i2c_dt_spec i2c;
};

struct mfd_m5pm1_data {
	struct k_sem lock;
};

int mfd_m5pm1_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct mfd_m5pm1_config *config = dev->config;

	return i2c_reg_read_byte_dt(&config->i2c, reg, val);
}

int mfd_m5pm1_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct mfd_m5pm1_config *config = dev->config;

	return i2c_reg_write_byte_dt(&config->i2c, reg, val);
}

int mfd_m5pm1_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	const struct mfd_m5pm1_config *config = dev->config;
	struct mfd_m5pm1_data *data = dev->data;
	uint8_t cur, new;
	int ret;

	k_sem_take(&data->lock, K_FOREVER);

	ret = i2c_reg_read_byte_dt(&config->i2c, reg, &cur);
	if (ret < 0) {
		goto out;
	}

	new = (cur & ~mask) | (val & mask);
	if (new != cur) {
		ret = i2c_reg_write_byte_dt(&config->i2c, reg, new);
	}

out:
	k_sem_give(&data->lock);
	return ret;
}

static int mfd_m5pm1_init(const struct device *dev)
{
	const struct mfd_m5pm1_config *config = dev->config;
	struct mfd_m5pm1_data *data = dev->data;
	uint8_t id, model, hw, sw, i2c_cfg;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus %s not ready", config->i2c.bus->name);
		return -ENODEV;
	}

	k_sem_init(&data->lock, 1, 1);

	ret = i2c_reg_read_byte_dt(&config->i2c, M5PM1_REG_DEVICE_ID, &id);
	if (ret < 0) {
		LOG_ERR("Failed to read device ID (%d)", ret);
		return ret;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, M5PM1_REG_DEVICE_MODEL, &model);
	if (ret < 0) {
		return ret;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, M5PM1_REG_HW_REV, &hw);
	if (ret < 0) {
		return ret;
	}

	ret = i2c_reg_read_byte_dt(&config->i2c, M5PM1_REG_SW_REV, &sw);
	if (ret < 0) {
		return ret;
	}

	LOG_INF("M5PM1 ID=0x%02x MODEL=0x%02x HW=0x%02x SW=0x%02x", id, model, hw, sw);

	/* Disable the I2C idle-sleep timeout so the PMIC stays awake and we
	 * never have to wake it before a transaction.
	 */
	ret = i2c_reg_read_byte_dt(&config->i2c, M5PM1_REG_I2C_CFG, &i2c_cfg);
	if (ret == 0 && (i2c_cfg & M5PM1_I2C_CFG_SLEEP_MASK) != 0) {
		i2c_cfg &= ~M5PM1_I2C_CFG_SLEEP_MASK;
		(void)i2c_reg_write_byte_dt(&config->i2c, M5PM1_REG_I2C_CFG, i2c_cfg);
	}

	return 0;
}

#define MFD_M5PM1_DEFINE(inst)                                                                     \
	static const struct mfd_m5pm1_config mfd_m5pm1_config_##inst = {                           \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
	};                                                                                         \
                                                                                                   \
	static struct mfd_m5pm1_data mfd_m5pm1_data_##inst;                                        \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, mfd_m5pm1_init, NULL, &mfd_m5pm1_data_##inst,                  \
			      &mfd_m5pm1_config_##inst, POST_KERNEL, CONFIG_MFD_INIT_PRIORITY,     \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_M5PM1_DEFINE)
