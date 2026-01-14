/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT csmic_si12t

#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_si12t, CONFIG_INPUT_LOG_LEVEL);

#define SI12T_SENSITIVITY1_ADDR 0x02
#define SI12T_SENSITIVITY2_ADDR 0x03
#define SI12T_SENSITIVITY3_ADDR 0x04
#define SI12T_SENSITIVITY4_ADDR 0x05
#define SI12T_SENSITIVITY5_ADDR 0x06
#define SI12T_SENSITIVITY6_ADDR 0x07
#define SI12T_CTRL1_ADDR        0x08
#define SI12T_CTRL2_ADDR        0x09
#define SI12T_REF_RST1_ADDR     0x0A
#define SI12T_REF_RST2_ADDR     0x0B
#define SI12T_CH_SENSE1_ADDR    0x0C
#define SI12T_CH_SENSE2_ADDR    0x0D
#define SI12T_CAL_CTRL1_ADDR    0x0E
#define SI12T_CAL_CTRL2_ADDR    0x0F
#define SI12T_OUTPUT1_ADDR      0x10

/* Touch status values (2 bits per channel in output registers) */
#define SI12T_STATUS_NONE       0  /* No touch detected */
#define SI12T_STATUS_LOW        1  /* Low strength touch */
#define SI12T_STATUS_MID        2  /* Medium strength touch */
#define SI12T_STATUS_HIGH       3  /* High strength touch */

#define SI12T_NUM_CHANNELS 12

struct si12t_config {
	struct i2c_dt_spec i2c;
#ifdef CONFIG_INPUT_SI12T_INTERRUPT
	struct gpio_dt_spec int_gpio;
#endif
	uint32_t input_codes[SI12T_NUM_CHANNELS];
	uint8_t num_codes;
	uint8_t sensitivity_level;
	bool sensitivity_high;
};

struct si12t_data {
	const struct device *dev;
	struct k_work work;
#ifdef CONFIG_INPUT_SI12T_INTERRUPT
	struct gpio_callback int_gpio_cb;
#else
	struct k_timer timer;
#endif
	/* Previous touch status for each channel (0=none, 1=low, 2=mid, 3=high) */
	uint8_t prev_status[SI12T_NUM_CHANNELS];
};

static int si12t_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct si12t_config *config = dev->config;
	return i2c_reg_write_byte_dt(&config->i2c, reg, val);
}

static void si12t_work_handler(struct k_work *work)
{
	struct si12t_data *data = CONTAINER_OF(work, struct si12t_data, work);
	const struct device *dev = data->dev;
	const struct si12t_config *config = dev->config;
	uint8_t output[3] = {0};
	int ret;

	/* Read only the output registers needed for configured channels */
	/* Each byte covers 4 channels: OUTPUT1=ch0-3, OUTPUT2=ch4-7, OUTPUT3=ch8-11 */
	uint8_t bytes_to_read = DIV_ROUND_UP(config->num_codes, 4);

	ret = i2c_burst_read_dt(&config->i2c, SI12T_OUTPUT1_ADDR, output, bytes_to_read);
	if (ret < 0) {
		LOG_ERR("Failed to read output registers");
		return;
	}

	/* Process channels (2 bits per channel) */
	/* OUTPUT1: ch 0-3, OUTPUT2: ch 4-7, OUTPUT3: ch 8-11 */
	for (int i = 0; i < config->num_codes; i++) {
		uint8_t byte_idx = i / 4;
		uint8_t bit_shift = (i % 4) * 2;
		uint8_t status = (output[byte_idx] >> bit_shift) & 0x03;

		/* Status: 0=None, 1=Low, 2=Mid, 3=High strength */
		/* Only report if status changed */
		if (status != data->prev_status[i]) {
			data->prev_status[i] = status;
			input_report(dev, INPUT_EV_KEY, config->input_codes[i],
				     status, true, K_FOREVER);
		}
	}
}

#ifdef CONFIG_INPUT_SI12T_INTERRUPT
static void si12t_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	struct si12t_data *data = CONTAINER_OF(cb, struct si12t_data, int_gpio_cb);
	k_work_submit(&data->work);
}
#else
static void si12t_timer_handler(struct k_timer *timer)
{
	struct si12t_data *data = CONTAINER_OF(timer, struct si12t_data, timer);
	k_work_submit(&data->work);
}
#endif

static int si12t_set_sensitivity(const struct device *dev)
{
	const struct si12t_config *config = dev->config;
	uint8_t val = 0x00;
	uint8_t level = config->sensitivity_level;

	if (config->sensitivity_high) {
		val = 0x88 | (level << 4) | level;
	} else {
		val = (level << 4) | level;
	}

	/* Set same sensitivity to all 6 registers (covering 12 channels) */
	for (uint8_t reg = SI12T_SENSITIVITY1_ADDR; reg <= SI12T_SENSITIVITY6_ADDR; reg++) {
		if (si12t_write_reg(dev, reg, val) < 0) {
			return -EIO;
		}
	}

	return 0;
}

static int si12t_init_chip(const struct device *dev)
{
	const struct si12t_config *config = dev->config;
	int ret;

	/*
	 * Build channel enable mask based on num_codes.
	 * Each register covers 8 channels (1 bit per channel).
	 * REF_RST1/CH_HOLD1/CAL_HOLD1: channels 0-7
	 * REF_RST2/CH_HOLD2/CAL_HOLD2: channels 8-11 (bits 0-3)
	 * A bit value of 0 enables the channel, 1 disables it.
	 */
	uint8_t enable_mask1;
	uint8_t enable_mask2;

	/* Enable only channels 0 to (num_codes - 1) */
	if (config->num_codes >= 8) {
		enable_mask1 = 0x00; /* Enable all 8 channels in first register */
		enable_mask2 = (~(BIT(config->num_codes - 8) - 1)) & 0x0F;
	} else {
		enable_mask1 = (~(BIT(config->num_codes) - 1)) & 0xFF;
		enable_mask2 = 0x0F; /* Disable all channels 8-11 */
	}

	/* Enable only configured channels (0 = enabled, 1 = disabled) */
	ret = si12t_write_reg(dev, SI12T_REF_RST1_ADDR, enable_mask1);
	if (ret < 0) {
		return ret;
	}
	ret = si12t_write_reg(dev, SI12T_REF_RST2_ADDR, enable_mask2);
	if (ret < 0) {
		return ret;
	}

	ret = si12t_write_reg(dev, SI12T_CH_SENSE1_ADDR, enable_mask1);
	if (ret < 0) {
		return ret;
	}
	ret = si12t_write_reg(dev, SI12T_CH_SENSE2_ADDR, enable_mask2);
	if (ret < 0) {
		return ret;
	}

	ret = si12t_write_reg(dev, SI12T_CAL_CTRL1_ADDR, enable_mask1);
	if (ret < 0) {
		return ret;
	}
	ret = si12t_write_reg(dev, SI12T_CAL_CTRL2_ADDR, enable_mask2);
	if (ret < 0) {
		return ret;
	}

	/* Set Control registers */
	/* S/W Reset Enable, Sleep Mode Enable */
	ret = si12t_write_reg(dev, SI12T_CTRL2_ADDR, 0x0F);
	if (ret < 0) {
		return ret;
	}
	ret = si12t_write_reg(dev, SI12T_CTRL2_ADDR, 0x07);
	if (ret < 0) {
		return ret;
	}

	/* Auto Mode, FTC=01, Interrupt(Middle,High), Response 4 */
	ret = si12t_write_reg(dev, SI12T_CTRL1_ADDR, 0x22);
	if (ret < 0) {
		return ret;
	}

	return si12t_set_sensitivity(dev);
}

static int si12t_init(const struct device *dev)
{
	const struct si12t_config *config = dev->config;
	struct si12t_data *data = dev->data;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	data->dev = dev;
	k_work_init(&data->work, si12t_work_handler);

	/* Initialize previous status to no touch */
	for (int i = 0; i < SI12T_NUM_CHANNELS; i++) {
		data->prev_status[i] = 0;
	}

#ifdef CONFIG_INPUT_SI12T_INTERRUPT
	if (!gpio_is_ready_dt(&config->int_gpio)) {
		LOG_ERR("Interrupt GPIO controller device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure interrupt GPIO");
		return ret;
	}

	gpio_init_callback(&data->int_gpio_cb, si12t_isr, BIT(config->int_gpio.pin));
	ret = gpio_add_callback(config->int_gpio.port, &data->int_gpio_cb);
	if (ret < 0) {
		LOG_ERR("Failed to add GPIO callback");
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Failed to configure GPIO interrupt");
		return ret;
	}
#else
	k_timer_init(&data->timer, si12t_timer_handler, NULL);
#endif

	ret = si12t_init_chip(dev);
	if (ret < 0) {
		return ret;
	}

#ifndef CONFIG_INPUT_SI12T_INTERRUPT
	k_timer_start(&data->timer, K_MSEC(CONFIG_INPUT_SI12T_PERIOD_MS),
		      K_MSEC(CONFIG_INPUT_SI12T_PERIOD_MS));
#endif

	return 0;
}

#ifdef CONFIG_INPUT_SI12T_INTERRUPT
#define SI12T_INT_GPIO(n) .int_gpio = GPIO_DT_SPEC_INST_GET(n, interrupt_gpios),
#else
#define SI12T_INT_GPIO(n)
#endif

#define SI12T_INIT(n)                                                                              \
	BUILD_ASSERT(IN_RANGE(DT_INST_PROP_LEN(n, input_codes), 1, SI12T_NUM_CHANNELS),            \
		     "input-codes must have 1 to 12 entries");                                     \
                                                                                                   \
	static const struct si12t_config si12t_config_##n = {                                      \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
		SI12T_INT_GPIO(n)                                                                  \
		.input_codes = DT_INST_PROP(n, input_codes),                                       \
		.num_codes = DT_INST_PROP_LEN(n, input_codes),                                     \
		.sensitivity_level = DT_INST_PROP(n, sensitivity_level),                           \
		.sensitivity_high =                                                                \
			DT_INST_ENUM_IDX(n, sensitivity_mode) == 1,                                \
	};                                                                                         \
                                                                                                   \
	static struct si12t_data si12t_data_##n;                                                   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, si12t_init, NULL, &si12t_data_##n, &si12t_config_##n,             \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(SI12T_INIT)
