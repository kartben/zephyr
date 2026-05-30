/*
 * Copyright (c) 2023 Grinn
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adi_ad559x

#include <errno.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/drivers/mfd/ad559x.h>

#include "mfd_ad559x.h"

bool mfd_ad559x_has_pointer_byte_map(const struct device *dev)
{
	const struct mfd_ad559x_config *config = dev->config;

	return config->has_pointer_byte_map;
}

int mfd_ad559x_read_raw(const struct device *dev, uint8_t *val, size_t len)
{
	struct mfd_ad559x_data *data = dev->data;

	return data->transfer_function->read_raw(dev, val, len);
}

int mfd_ad559x_write_raw(const struct device *dev, uint8_t *val, size_t len)
{
	struct mfd_ad559x_data *data = dev->data;

	return data->transfer_function->write_raw(dev, val, len);
}

int mfd_ad559x_read_reg(const struct device *dev, uint8_t reg, uint8_t reg_data, uint16_t *val)
{
	struct mfd_ad559x_data *data = dev->data;

	return data->transfer_function->read_reg(dev, reg, reg_data, val);
}

int mfd_ad559x_write_reg(const struct device *dev, uint8_t reg, uint16_t val)
{
	struct mfd_ad559x_data *data = dev->data;

	return data->transfer_function->write_reg(dev, reg, val);
}

static int mfd_ad559x_pin_check(uint8_t pin)
{
	if (pin >= AD559X_PIN_MAX) {
		return -EINVAL;
	}

	return 0;
}

static int mfd_ad559x_write_function_regs(const struct device *dev, uint8_t adc_conf, uint8_t dac_conf,
					  uint8_t gpio_out, uint8_t gpio_in,
					  uint8_t gpio_pull_down)
{
	int ret;

	ret = mfd_ad559x_write_reg(dev, AD559X_REG_GPIO_PULLDOWN, gpio_pull_down);
	if (ret < 0) {
		return ret;
	}

	ret = mfd_ad559x_write_reg(dev, AD559X_REG_GPIO_OUTPUT_EN, gpio_out);
	if (ret < 0) {
		return ret;
	}

	ret = mfd_ad559x_write_reg(dev, AD559X_REG_GPIO_INPUT_EN, gpio_in);
	if (ret < 0) {
		return ret;
	}

	ret = mfd_ad559x_write_reg(dev, AD559X_REG_LDAC_EN, dac_conf);
	if (ret < 0) {
		return ret;
	}

	return mfd_ad559x_write_reg(dev, AD559X_REG_ADC_CONFIG, adc_conf);
}

int mfd_ad559x_adc_channel_setup(const struct device *dev, const struct device *client_dev,
				 uint8_t channel)
{
	struct mfd_ad559x_data *data = dev->data;
	uint8_t pin_mask = BIT(channel);
	uint8_t adc_conf;
	uint8_t dac_conf;
	uint8_t gpio_out;
	uint8_t gpio_in;
	uint8_t gpio_pull_down;
	int ret;

	ret = mfd_ad559x_pin_check(channel);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->pin_owner[channel] != NULL) && (data->pin_owner[channel] != client_dev)) {
		ret = -EBUSY;
		goto out;
	}

	if ((data->pin_owner[channel] == client_dev) &&
	    (data->pin_mode[channel] == AD559X_PIN_MODE_ADC)) {
		ret = 0;
		goto out;
	}

	adc_conf = data->adc_conf | pin_mask;
	dac_conf = data->dac_conf & ~pin_mask;
	gpio_out = data->gpio_out & ~pin_mask;
	gpio_in = data->gpio_in & ~pin_mask;
	gpio_pull_down = data->gpio_pull_down & ~pin_mask;

	ret = mfd_ad559x_write_function_regs(dev, adc_conf, dac_conf, gpio_out, gpio_in,
					     gpio_pull_down);
	if (ret == 0) {
		data->adc_conf = adc_conf;
		data->dac_conf = dac_conf;
		data->gpio_out = gpio_out;
		data->gpio_in = gpio_in;
		data->gpio_pull_down = gpio_pull_down;
		data->pin_owner[channel] = client_dev;
		data->pin_mode[channel] = AD559X_PIN_MODE_ADC;
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_ad559x_dac_channel_setup(const struct device *dev, const struct device *client_dev,
				 uint8_t channel)
{
	struct mfd_ad559x_data *data = dev->data;
	uint8_t pin_mask = BIT(channel);
	uint8_t adc_conf;
	uint8_t dac_conf;
	uint8_t gpio_out;
	uint8_t gpio_in;
	uint8_t gpio_pull_down;
	int ret;

	ret = mfd_ad559x_pin_check(channel);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->pin_owner[channel] != NULL) && (data->pin_owner[channel] != client_dev)) {
		ret = -EBUSY;
		goto out;
	}

	if ((data->pin_owner[channel] == client_dev) &&
	    (data->pin_mode[channel] == AD559X_PIN_MODE_DAC)) {
		ret = 0;
		goto out;
	}

	adc_conf = data->adc_conf & ~pin_mask;
	dac_conf = data->dac_conf | pin_mask;
	gpio_out = data->gpio_out & ~pin_mask;
	gpio_in = data->gpio_in & ~pin_mask;
	gpio_pull_down = data->gpio_pull_down & ~pin_mask;

	ret = mfd_ad559x_write_function_regs(dev, adc_conf, dac_conf, gpio_out, gpio_in,
					     gpio_pull_down);
	if (ret == 0) {
		data->adc_conf = adc_conf;
		data->dac_conf = dac_conf;
		data->gpio_out = gpio_out;
		data->gpio_in = gpio_in;
		data->gpio_pull_down = gpio_pull_down;
		data->pin_owner[channel] = client_dev;
		data->pin_mode[channel] = AD559X_PIN_MODE_DAC;
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_ad559x_gpio_pin_configure(const struct device *dev, const struct device *client_dev,
				  uint8_t pin, gpio_flags_t flags, uint8_t value)
{
	struct mfd_ad559x_data *data = dev->data;
	uint8_t pin_mask = BIT(pin);
	uint8_t adc_conf;
	uint8_t dac_conf;
	uint8_t gpio_out;
	uint8_t gpio_in;
	uint8_t gpio_pull_down;
	int ret;

	ret = mfd_ad559x_pin_check(pin);
	if (ret < 0) {
		return ret;
	}

	if ((flags & GPIO_PULL_UP) != 0U) {
		return -ENOTSUP;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if ((data->pin_owner[pin] != NULL) && (data->pin_owner[pin] != client_dev)) {
		ret = -EBUSY;
		goto out;
	}

	adc_conf = data->adc_conf & ~pin_mask;
	dac_conf = data->dac_conf & ~pin_mask;
	gpio_out = data->gpio_out & ~pin_mask;
	gpio_in = data->gpio_in & ~pin_mask;
	gpio_pull_down = data->gpio_pull_down & ~pin_mask;

	if ((flags & GPIO_OUTPUT) != 0U) {
		ret = mfd_ad559x_write_reg(dev, AD559X_REG_GPIO_SET, value);
		if (ret < 0) {
			goto out;
		}

		gpio_out |= pin_mask;
	} else if ((flags & GPIO_INPUT) != 0U) {
		gpio_in |= pin_mask;

		if ((flags & GPIO_PULL_DOWN) != 0U) {
			gpio_pull_down |= pin_mask;
		}
	} else {
		ret = -ENOTSUP;
		goto out;
	}

	ret = mfd_ad559x_write_function_regs(dev, adc_conf, dac_conf, gpio_out, gpio_in,
					     gpio_pull_down);
	if (ret == 0) {
		data->adc_conf = adc_conf;
		data->dac_conf = dac_conf;
		data->gpio_out = gpio_out;
		data->gpio_in = gpio_in;
		data->gpio_pull_down = gpio_pull_down;
		data->pin_owner[pin] = client_dev;
		data->pin_mode[pin] = AD559X_PIN_MODE_GPIO;
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_ad559x_gpio_pin_release(const struct device *dev, const struct device *client_dev, uint8_t pin)
{
	struct mfd_ad559x_data *data = dev->data;
	uint8_t pin_mask = BIT(pin);
	uint8_t adc_conf;
	uint8_t dac_conf;
	uint8_t gpio_out;
	uint8_t gpio_in;
	uint8_t gpio_pull_down;
	int ret;

	ret = mfd_ad559x_pin_check(pin);
	if (ret < 0) {
		return ret;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->pin_owner[pin] == NULL) {
		ret = 0;
		goto out;
	}

	if (data->pin_owner[pin] != client_dev) {
		ret = -EBUSY;
		goto out;
	}

	adc_conf = data->adc_conf & ~pin_mask;
	dac_conf = data->dac_conf & ~pin_mask;
	gpio_out = data->gpio_out & ~pin_mask;
	gpio_in = data->gpio_in & ~pin_mask;
	gpio_pull_down = data->gpio_pull_down & ~pin_mask;

	ret = mfd_ad559x_write_function_regs(dev, adc_conf, dac_conf, gpio_out, gpio_in,
					     gpio_pull_down);
	if (ret == 0) {
		data->adc_conf = adc_conf;
		data->dac_conf = dac_conf;
		data->gpio_out = gpio_out;
		data->gpio_in = gpio_in;
		data->gpio_pull_down = gpio_pull_down;
		data->pin_owner[pin] = NULL;
		data->pin_mode[pin] = AD559X_PIN_MODE_NONE;
	}

out:
	k_mutex_unlock(&data->lock);

	return ret;
}

int mfd_ad559x_gpio_port_get_raw(const struct device *dev, uint16_t *value)
{
	struct mfd_ad559x_data *data = dev->data;
	uint16_t reg_val;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (mfd_ad559x_has_pointer_byte_map(dev)) {
		ret = mfd_ad559x_read_reg(dev, AD559X_GPIO_RD_POINTER, 0, &reg_val);
		reg_val &= BIT_MASK(AD559X_PIN_MAX);
	} else {
		ret = mfd_ad559x_read_reg(dev, AD559X_REG_GPIO_INPUT_EN, data->gpio_in, &reg_val);
	}

	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		return ret;
	}

	*value = reg_val & BIT_MASK(AD559X_PIN_MAX);

	return 0;
}

static int mfd_add559x_software_reset(const struct device *dev)
{
	return mfd_ad559x_write_reg(dev, AD559X_REG_SOFTWARE_RESET,
				    AD559X_SOFTWARE_RESET_MAGIC_VAL);
}

static int mfd_ad559x_init(const struct device *dev)
{
	const struct mfd_ad559x_config *config = dev->config;
	struct mfd_ad559x_data *data = dev->data;
	int ret;

	ret = config->bus_init(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->reset_gpio.port) {
		if (!gpio_is_ready_dt(&config->reset_gpio)) {
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	ret = mfd_add559x_software_reset(dev);
	if (ret < 0) {
		return ret;
	}

	k_mutex_init(&data->lock);

	return 0;
}

#define MDF_AD559X_DEFINE_I2C_BUS(inst)                                                            \
	.i2c = I2C_DT_SPEC_INST_GET(inst), .bus_init = mfd_ad559x_i2c_init,                        \
	.has_pointer_byte_map = true

#define MDF_AD559X_DEFINE_SPI_BUS_FLAGS                                                            \
	(SPI_WORD_SET(8) | SPI_TRANSFER_MSB | SPI_OP_MODE_MASTER | SPI_MODE_CPOL)

#define MDF_AD559X_DEFINE_SPI_BUS(inst)                                                            \
	.spi = SPI_DT_SPEC_INST_GET(inst, MDF_AD559X_DEFINE_SPI_BUS_FLAGS),                        \
	.bus_init = mfd_ad559x_spi_init, .has_pointer_byte_map = false

#define MFD_AD559X_DEFINE_BUS(inst)                                                                \
	COND_CODE_1(DT_INST_ON_BUS(inst, i2c), (MDF_AD559X_DEFINE_I2C_BUS(inst)),                  \
		    (MDF_AD559X_DEFINE_SPI_BUS(inst)))

#define MFD_AD559X_DEFINE(inst)                                                                    \
	static struct mfd_ad559x_data mfd_ad559x_data_##inst;                                      \
	static const struct mfd_ad559x_config mfd_ad559x_config_##inst = {                         \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),                    \
		MFD_AD559X_DEFINE_BUS(inst),                                                       \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, mfd_ad559x_init, NULL, &mfd_ad559x_data_##inst,                \
			      &mfd_ad559x_config_##inst, POST_KERNEL, CONFIG_MFD_INIT_PRIORITY,    \
			      NULL);

DT_INST_FOREACH_STATUS_OKAY(MFD_AD559X_DEFINE);
