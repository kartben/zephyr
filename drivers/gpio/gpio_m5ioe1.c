/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT m5stack_m5ioe1_gpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/mfd/m5ioe1.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(gpio_m5ioe1, CONFIG_GPIO_LOG_LEVEL);

/* Each register is the low half of a pair covering IO1..IO14, see mfd_m5ioe1_update_reg16(). */
#define M5IOE1_REG_GPIO_M   0x03
#define M5IOE1_REG_GPIO_O   0x05
#define M5IOE1_REG_GPIO_I   0x07
#define M5IOE1_REG_GPIO_PU  0x09
#define M5IOE1_REG_GPIO_PD  0x0b
#define M5IOE1_REG_GPIO_DRV 0x13

struct gpio_m5ioe1_config {
	struct gpio_driver_config common;
	const struct device *mfd;
};

struct gpio_m5ioe1_data {
	struct gpio_driver_data common;
};

static int gpio_m5ioe1_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_m5ioe1_config *config = dev->config;
	uint16_t bit = BIT(pin);
	int ret;

	if ((config->common.port_pin_mask & bit) == 0U) {
		return -EINVAL;
	}

	if ((flags & GPIO_INPUT) != 0 && (flags & GPIO_OUTPUT) != 0) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_SINGLE_ENDED) != 0 && (flags & GPIO_LINE_OPEN_DRAIN) == 0) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_PULL_UP) != 0 && (flags & GPIO_PULL_DOWN) != 0) {
		return -ENOTSUP;
	}

	ret = mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_PU, bit,
				      (flags & GPIO_PULL_UP) != 0 ? bit : 0U);
	if (ret < 0) {
		return ret;
	}

	ret = mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_PD, bit,
				      (flags & GPIO_PULL_DOWN) != 0 ? bit : 0U);
	if (ret < 0) {
		return ret;
	}

	if ((flags & GPIO_OUTPUT) == 0) {
		/* Input, or disconnected when no pull is requested either */
		return mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_M, bit, 0U);
	}

	ret = mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_DRV, bit,
				      (flags & GPIO_SINGLE_ENDED) != 0 ? bit : 0U);
	if (ret < 0) {
		return ret;
	}

	if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
		ret = mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_O, bit, bit);
	} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
		ret = mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_O, bit, 0U);
	}
	if (ret < 0) {
		return ret;
	}

	return mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_M, bit, bit);
}

static int gpio_m5ioe1_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_m5ioe1_config *config = dev->config;
	uint16_t port;
	int ret;

	ret = mfd_m5ioe1_read_reg16(config->mfd, M5IOE1_REG_GPIO_I, &port);
	if (ret < 0) {
		return ret;
	}

	*value = port & config->common.port_pin_mask;

	return 0;
}

static int gpio_m5ioe1_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					   gpio_port_value_t value)
{
	const struct gpio_m5ioe1_config *config = dev->config;

	return mfd_m5ioe1_update_reg16(config->mfd, M5IOE1_REG_GPIO_O,
				       mask & config->common.port_pin_mask, value);
}

static int gpio_m5ioe1_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return gpio_m5ioe1_port_set_masked_raw(dev, pins, pins);
}

static int gpio_m5ioe1_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	return gpio_m5ioe1_port_set_masked_raw(dev, pins, 0);
}

static int gpio_m5ioe1_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_m5ioe1_config *config = dev->config;

	return mfd_m5ioe1_toggle_reg16(config->mfd, M5IOE1_REG_GPIO_O,
				       pins & config->common.port_pin_mask);
}

static DEVICE_API(gpio, gpio_m5ioe1_api) = {
	.pin_configure = gpio_m5ioe1_configure,
	.port_get_raw = gpio_m5ioe1_port_get_raw,
	.port_set_masked_raw = gpio_m5ioe1_port_set_masked_raw,
	.port_set_bits_raw = gpio_m5ioe1_port_set_bits_raw,
	.port_clear_bits_raw = gpio_m5ioe1_port_clear_bits_raw,
	.port_toggle_bits = gpio_m5ioe1_port_toggle_bits,
};

static int gpio_m5ioe1_init(const struct device *dev)
{
	const struct gpio_m5ioe1_config *config = dev->config;

	if (!device_is_ready(config->mfd)) {
		LOG_ERR_DEVICE_NOT_READY(config->mfd);
		return -ENODEV;
	}

	return 0;
}

#define GPIO_M5IOE1_DEFINE(inst)                                                                   \
	static const struct gpio_m5ioe1_config gpio_m5ioe1_config_##inst = {                       \
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(inst),                                   \
		.mfd = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                        \
	};                                                                                         \
	static struct gpio_m5ioe1_data gpio_m5ioe1_data_##inst;                                    \
	DEVICE_DT_INST_DEFINE(inst, gpio_m5ioe1_init, NULL, &gpio_m5ioe1_data_##inst,              \
			      &gpio_m5ioe1_config_##inst, POST_KERNEL, CONFIG_MFD_INIT_PRIORITY,   \
			      &gpio_m5ioe1_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_M5IOE1_DEFINE)
