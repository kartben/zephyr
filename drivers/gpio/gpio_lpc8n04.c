/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT nxp_lpc8n04_gpio

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util.h>

#include <soc.h>

/* GPIODATA[mask] reads and writes only the pins set in the 12-bit address mask. */
#define LPC8N04_GPIO_ALL_PINS 0xFFFU

/* I2CMODE value selecting the open-drain GPIO pad mode. */
#define LPC8N04_IOCON_I2CMODE_GPIO 1U

struct gpio_lpc8n04_config {
	struct gpio_driver_config common;
	GPIO_Type *base;
	volatile uint32_t *iocon;
	/* Open-drain I2C pads: no pull resistors, GPIO mode set through I2CMODE. */
	uint32_t i2c_pads;
	const struct device *clock_dev;
	clock_control_subsys_t clock_subsys;
	void (*irq_config)(void);
};

struct gpio_lpc8n04_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
};

static int gpio_lpc8n04_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_lpc8n04_config *config = dev->config;
	GPIO_Type *base = config->base;
	const uint32_t mask = BIT(pin);
	bool i2c_pad = (mask & config->i2c_pads) != 0U;
	uint32_t iocon;
	unsigned int key;

	if ((flags & GPIO_SINGLE_ENDED) != 0U) {
		if (!i2c_pad || (flags & GPIO_LINE_OPEN_DRAIN) == 0U) {
			return -ENOTSUP;
		}
	} else if (i2c_pad && (flags & GPIO_OUTPUT) != 0U) {
		return -ENOTSUP;
	}

	if ((flags & (GPIO_PULL_UP | GPIO_PULL_DOWN)) != 0U && i2c_pad) {
		return -ENOTSUP;
	}

	iocon = config->iocon[pin] & ~IOCON_PIO_FUNC_MASK;
	if (i2c_pad) {
		iocon &= ~IOCON_PIO_I2CMODE_MASK;
		iocon |= IOCON_PIO_I2CMODE(LPC8N04_IOCON_I2CMODE_GPIO);
	} else {
		iocon &= ~IOCON_PIO_MODE_MASK;
		if ((flags & GPIO_PULL_UP) != 0U) {
			iocon |= IOCON_PIO_MODE(2U);
		} else if ((flags & GPIO_PULL_DOWN) != 0U) {
			iocon |= IOCON_PIO_MODE(1U);
		}
	}
	config->iocon[pin] = iocon;

	key = irq_lock();

	if ((flags & GPIO_OUTPUT) != 0U) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			base->GPIODATA[mask] = mask;
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			base->GPIODATA[mask] = 0U;
		}
		base->DIR |= mask;
	} else {
		base->DIR &= ~mask;
	}

	irq_unlock(key);

	return 0;
}

static int gpio_lpc8n04_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_lpc8n04_config *config = dev->config;

	*value = config->base->GPIODATA[LPC8N04_GPIO_ALL_PINS];

	return 0;
}

static int gpio_lpc8n04_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					    gpio_port_value_t value)
{
	const struct gpio_lpc8n04_config *config = dev->config;

	config->base->GPIODATA[mask & LPC8N04_GPIO_ALL_PINS] = value;

	return 0;
}

static int gpio_lpc8n04_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_lpc8n04_config *config = dev->config;

	config->base->GPIODATA[pins & LPC8N04_GPIO_ALL_PINS] = pins;

	return 0;
}

static int gpio_lpc8n04_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_lpc8n04_config *config = dev->config;

	config->base->GPIODATA[pins & LPC8N04_GPIO_ALL_PINS] = 0U;

	return 0;
}

static int gpio_lpc8n04_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_lpc8n04_config *config = dev->config;
	volatile uint32_t *data = &config->base->GPIODATA[pins & LPC8N04_GPIO_ALL_PINS];
	unsigned int key = irq_lock();

	*data = ~*data;

	irq_unlock(key);

	return 0;
}

static int gpio_lpc8n04_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
						enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_lpc8n04_config *config = dev->config;
	GPIO_Type *base = config->base;
	const uint32_t mask = BIT(pin);
	unsigned int key;

	key = irq_lock();

	base->IMSC &= ~mask;

	if (mode != GPIO_INT_MODE_DISABLED) {
		if (mode == GPIO_INT_MODE_LEVEL) {
			base->IS |= mask;
		} else {
			base->IS &= ~mask;
		}

		if (trig == GPIO_INT_TRIG_BOTH) {
			base->IBE |= mask;
		} else {
			base->IBE &= ~mask;
			if (trig == GPIO_INT_TRIG_HIGH) {
				base->IEV |= mask;
			} else {
				base->IEV &= ~mask;
			}
		}

		base->IC = mask;
		base->IMSC |= mask;
	}

	irq_unlock(key);

	return 0;
}

static int gpio_lpc8n04_manage_callback(const struct device *dev, struct gpio_callback *callback,
					bool set)
{
	struct gpio_lpc8n04_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static uint32_t gpio_lpc8n04_get_pending_int(const struct device *dev)
{
	const struct gpio_lpc8n04_config *config = dev->config;

	return config->base->MIS;
}

static void gpio_lpc8n04_isr(const struct device *dev)
{
	const struct gpio_lpc8n04_config *config = dev->config;
	struct gpio_lpc8n04_data *data = dev->data;
	uint32_t status = config->base->MIS;

	/* Clears edge events; level events stay pending while the level holds. */
	config->base->IC = status;

	gpio_fire_callbacks(&data->callbacks, dev, status);
}

static int gpio_lpc8n04_init(const struct device *dev)
{
	const struct gpio_lpc8n04_config *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev, config->clock_subsys);
	if (ret != 0) {
		return ret;
	}

	config->base->IMSC = 0U;
	config->base->IC = LPC8N04_GPIO_ALL_PINS;

	config->irq_config();

	return 0;
}

static DEVICE_API(gpio, gpio_lpc8n04_api) = {
	.pin_configure = gpio_lpc8n04_pin_configure,
	.port_get_raw = gpio_lpc8n04_port_get_raw,
	.port_set_masked_raw = gpio_lpc8n04_port_set_masked_raw,
	.port_set_bits_raw = gpio_lpc8n04_port_set_bits_raw,
	.port_clear_bits_raw = gpio_lpc8n04_port_clear_bits_raw,
	.port_toggle_bits = gpio_lpc8n04_port_toggle_bits,
	.pin_interrupt_configure = gpio_lpc8n04_pin_interrupt_configure,
	.manage_callback = gpio_lpc8n04_manage_callback,
	.get_pending_int = gpio_lpc8n04_get_pending_int,
};

#define GPIO_LPC8N04_PIN_BIT(node_id, prop, idx) | BIT(DT_PROP_BY_IDX(node_id, prop, idx))

#define GPIO_LPC8N04_I2C_PADS(n)                                                                   \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(n, nxp_i2c_pads),                                        \
		    (0U DT_INST_FOREACH_PROP_ELEM(n, nxp_i2c_pads, GPIO_LPC8N04_PIN_BIT)), (0U))

#define GPIO_LPC8N04_INIT(n)                                                                       \
	static void gpio_lpc8n04_irq_config_##n(void)                                              \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), gpio_lpc8n04_isr,           \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static const struct gpio_lpc8n04_config gpio_lpc8n04_config_##n = {                        \
		.common = GPIO_COMMON_CONFIG_FROM_DT_INST(n),                                      \
		.base = (GPIO_Type *)DT_INST_REG_ADDR(n),                                          \
		.iocon = (volatile uint32_t *)DT_REG_ADDR(DT_INST_PHANDLE(n, nxp_iocon)),          \
		.i2c_pads = GPIO_LPC8N04_I2C_PADS(n),                                              \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),                                \
		.clock_subsys = (clock_control_subsys_t)DT_INST_CLOCKS_CELL(n, name),              \
		.irq_config = gpio_lpc8n04_irq_config_##n,                                         \
	};                                                                                         \
                                                                                                   \
	static struct gpio_lpc8n04_data gpio_lpc8n04_data_##n;                                     \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_lpc8n04_init, NULL, &gpio_lpc8n04_data_##n,                  \
			      &gpio_lpc8n04_config_##n, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,   \
			      &gpio_lpc8n04_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_LPC8N04_INIT)
