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
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(mfd_m5pm1, CONFIG_MFD_LOG_LEVEL);

#define M5PM1_REG_DEVICE_ID    0x00
#define M5PM1_REG_DEVICE_MODEL 0x01
#define M5PM1_REG_GPIO_FUNC0    0x16
#define M5PM1_REG_GPIO_FUNC1    0x17
#define M5PM1_REG_GPIO_WAKE_EN  0x18
#define M5PM1_REG_GPIO_WAKE_CFG 0x19

#define M5PM1_DEVICE_ID    0x50
#define M5PM1_DEVICE_MODEL 0x20

/*
 * Each pin occupies a 2-bit field. Pins 0..3 are packed into FUNC0; pin 4
 * lives alone at offset 0 of FUNC1.
 */
#define M5PM1_GPIO_FUNC_REG(pin)    ((pin) == 4U ? M5PM1_REG_GPIO_FUNC1 : M5PM1_REG_GPIO_FUNC0)
#define M5PM1_GPIO_FIELD_SHIFT(pin) (((pin) == 4U ? 0U : (pin)) * 2U)
#define M5PM1_GPIO_FIELD_MASK(pin)  (0x3U << M5PM1_GPIO_FIELD_SHIFT(pin))

struct m5pm1_config {
	struct i2c_dt_spec i2c;
};

struct m5pm1_data {
	struct k_mutex lock;
};

static int m5pm1_read_reg_locked(const struct device *dev, uint8_t reg, uint8_t *val)
{
	const struct m5pm1_config *config = dev->config;

	return i2c_reg_read_byte_dt(&config->i2c, reg, val);
}

static int m5pm1_write_reg_locked(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct m5pm1_config *config = dev->config;

	return i2c_reg_write_byte_dt(&config->i2c, reg, val);
}

static int m5pm1_update_reg_locked(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	const struct m5pm1_config *config = dev->config;

	return i2c_reg_update_byte_dt(&config->i2c, reg, mask, val);
}

static int m5pm1_burst_read_locked(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	const struct m5pm1_config *config = dev->config;

	return i2c_burst_read_dt(&config->i2c, reg, buf, len);
}

static bool m5pm1_gpio_function_supported(uint8_t pin, enum m5pm1_gpio_func func)
{
	switch (func) {
	case M5PM1_GPIO_FUNC_GPIO:
	case M5PM1_GPIO_FUNC_IRQ:
	case M5PM1_GPIO_FUNC_SPECIAL:
		return pin < M5PM1_GPIO_PIN_COUNT;
	case M5PM1_GPIO_FUNC_WAKE:
		return pin < M5PM1_GPIO_PIN_COUNT && pin != 1U;
	default:
		return false;
	}
}

static int m5pm1_gpio_wake_peer(uint8_t pin)
{
	switch (pin) {
	case 0U:
		return 2;
	case 2U:
		return 0;
	case 3U:
		return 4;
	case 4U:
		return 3;
	default:
		return -1;
	}
}

static int m5pm1_set_gpio_function_locked(const struct device *dev, uint8_t pin,
					  enum m5pm1_gpio_func func)
{
	int peer;
	int ret;
	uint8_t reg_val;
	uint8_t shift;
	uint8_t mask;

	if (pin >= M5PM1_GPIO_PIN_COUNT) {
		return -EINVAL;
	}

	if (!m5pm1_gpio_function_supported(pin, func)) {
		return -ENOTSUP;
	}

	if (func == M5PM1_GPIO_FUNC_WAKE) {
		peer = m5pm1_gpio_wake_peer(pin);
		if (peer >= 0) {
			ret = m5pm1_read_reg_locked(dev, M5PM1_GPIO_FUNC_REG(peer), &reg_val);
			if (ret < 0) {
				return ret;
			}

			shift = M5PM1_GPIO_FIELD_SHIFT(peer);
			mask = M5PM1_GPIO_FIELD_MASK(peer);
			if (((reg_val & mask) >> shift) == M5PM1_GPIO_FUNC_WAKE) {
				return -EBUSY;
			}
		}
	}

	shift = M5PM1_GPIO_FIELD_SHIFT(pin);
	mask = M5PM1_GPIO_FIELD_MASK(pin);

	return m5pm1_update_reg_locked(dev, M5PM1_GPIO_FUNC_REG(pin), mask,
				       (uint8_t)(func << shift));
}

int mfd_m5pm1_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = m5pm1_read_reg_locked(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = m5pm1_write_reg_locked(dev, reg, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_update_reg(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t val)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = m5pm1_update_reg_locked(dev, reg, mask, val);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_burst_read(const struct device *dev, uint8_t reg, uint8_t *buf, size_t len)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = m5pm1_burst_read_locked(dev, reg, buf, len);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_set_gpio_function(const struct device *dev, uint8_t pin, enum m5pm1_gpio_func func)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = m5pm1_set_gpio_function_locked(dev, pin, func);
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_m5pm1_configure_gpio_wake(const struct device *dev, uint8_t pin, bool enable,
				  bool rising_edge)
{
	struct m5pm1_data *data = dev->data;
	int ret;

	if (pin >= M5PM1_GPIO_PIN_COUNT) {
		return -EINVAL;
	}

	if (!m5pm1_gpio_function_supported(pin, M5PM1_GPIO_FUNC_WAKE)) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (enable) {
		ret = m5pm1_set_gpio_function_locked(dev, pin, M5PM1_GPIO_FUNC_WAKE);
		if (ret < 0) {
			goto out;
		}
	}

	ret = m5pm1_update_reg_locked(dev, M5PM1_REG_GPIO_WAKE_CFG, BIT(pin),
				      rising_edge ? BIT(pin) : 0U);
	if (ret < 0) {
		goto out;
	}

	ret = m5pm1_update_reg_locked(dev, M5PM1_REG_GPIO_WAKE_EN, BIT(pin),
				      enable ? BIT(pin) : 0U);
out:
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
