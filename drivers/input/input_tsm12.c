/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT adsemi_tsm12

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_tsm12, CONFIG_INPUT_LOG_LEVEL);

#define TSM12_SENSITIVITY1 0x02U
#define TSM12_CTRL1        0x08U
#define TSM12_CTRL2        0x09U
#define TSM12_REF_RST1     0x0AU
#define TSM12_REF_RST2     0x0BU
#define TSM12_CH_HOLD1     0x0CU
#define TSM12_CH_HOLD2     0x0DU
#define TSM12_CAL_HOLD1    0x0EU
#define TSM12_CAL_HOLD2    0x0FU
#define TSM12_OUTPUT1      0x10U

/* Auto mode, FTC 10 s, interrupt on low, middle or high output, response period 4 */
#define TSM12_CTRL1_VAL 0x2AU

/* Bits 1:0 must always be written as 0b11 */
#define TSM12_CTRL2_SRST  BIT(3)
#define TSM12_CTRL2_SLEEP BIT(2)
#define TSM12_CTRL2_INIT  0x03U

#define TSM12_SENS_HL BIT(3)

#define TSM12_NUM_CHANNELS  12U
#define TSM12_CH_PER_OUTPUT 4U
#define TSM12_OUTPUT_MASK   0x03U
#define TSM12_MIDPOINT_MAX  7U

struct tsm12_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec int_gpio;
	uint16_t input_codes[TSM12_NUM_CHANNELS];
	uint8_t sensitivity[TSM12_NUM_CHANNELS];
	uint8_t num_codes;
};

struct tsm12_data {
	const struct device *dev;
	struct k_work_delayable work;
	struct gpio_callback int_gpio_cb;
	uint16_t pressed;
};

static void tsm12_work_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct tsm12_data *data = CONTAINER_OF(dwork, struct tsm12_data, work);
	const struct device *dev = data->dev;
	const struct tsm12_config *config = dev->config;
	uint8_t output[TSM12_NUM_CHANNELS / TSM12_CH_PER_OUTPUT];
	uint16_t pressed = 0U;
	uint16_t changed;
	int ret;

	ret = i2c_burst_read_dt(&config->i2c, TSM12_OUTPUT1, output,
				DIV_ROUND_UP(config->num_codes, TSM12_CH_PER_OUTPUT));
	if (ret < 0) {
		LOG_ERR("Failed to read output registers: %d", ret);
		k_work_reschedule(dwork, K_MSEC(CONFIG_INPUT_TSM12_PERIOD_MS));
		return;
	}

	for (uint8_t i = 0U; i < config->num_codes; i++) {
		uint8_t shift = (i % TSM12_CH_PER_OUTPUT) * 2U;

		if (((output[i / TSM12_CH_PER_OUTPUT] >> shift) & TSM12_OUTPUT_MASK) != 0U) {
			pressed |= BIT(i);
		}
	}

	changed = pressed ^ data->pressed;
	data->pressed = pressed;

	for (uint8_t i = 0U; i < config->num_codes; i++) {
		if ((changed & BIT(i)) != 0U) {
			changed &= ~BIT(i);
			input_report_key(dev, config->input_codes[i], (pressed & BIT(i)) != 0U,
					 changed == 0U, K_FOREVER);
		}
	}

	/* The interrupt only signals touches, so poll until every channel is released */
	if ((config->int_gpio.port == NULL) || (pressed != 0U)) {
		k_work_reschedule(dwork, K_MSEC(CONFIG_INPUT_TSM12_PERIOD_MS));
	}
}

static void tsm12_isr(const struct device *port, struct gpio_callback *cb, uint32_t pins)
{
	struct tsm12_data *data = CONTAINER_OF(cb, struct tsm12_data, int_gpio_cb);

	ARG_UNUSED(port);
	ARG_UNUSED(pins);

	k_work_reschedule(&data->work, K_NO_WAIT);
}

static int tsm12_init_chip(const struct device *dev)
{
	const struct tsm12_config *config = dev->config;
	const uint8_t *s = config->sensitivity;
	/* Hold, and keep resetting the reference of, channels without an input code */
	uint16_t unused = (uint16_t)(BIT_MASK(TSM12_NUM_CHANNELS) & ~BIT_MASK(config->num_codes));
	uint8_t unused_lo = (uint8_t)(unused & 0xFFU);
	uint8_t unused_hi = (uint8_t)(unused >> 8);
	/* Initialization flow from the datasheet: reset, sensitivity, CTRL1, then release */
	const uint8_t seq[][2] = {
		{TSM12_CTRL2, TSM12_CTRL2_SRST | TSM12_CTRL2_SLEEP | TSM12_CTRL2_INIT},
		{TSM12_CTRL2, TSM12_CTRL2_SLEEP | TSM12_CTRL2_INIT},
		/* Two channels per register, the higher-numbered one in the upper nibble */
		{TSM12_SENSITIVITY1 + 0U, (uint8_t)((s[1] << 4) | s[0])},
		{TSM12_SENSITIVITY1 + 1U, (uint8_t)((s[3] << 4) | s[2])},
		{TSM12_SENSITIVITY1 + 2U, (uint8_t)((s[5] << 4) | s[4])},
		{TSM12_SENSITIVITY1 + 3U, (uint8_t)((s[7] << 4) | s[6])},
		{TSM12_SENSITIVITY1 + 4U, (uint8_t)((s[9] << 4) | s[8])},
		{TSM12_SENSITIVITY1 + 5U, (uint8_t)((s[11] << 4) | s[10])},
		{TSM12_CTRL1, TSM12_CTRL1_VAL},
		{TSM12_CH_HOLD1, unused_lo},
		{TSM12_CH_HOLD2, unused_hi},
		{TSM12_CAL_HOLD1, unused_lo},
		{TSM12_CAL_HOLD2, unused_hi},
		{TSM12_REF_RST1, unused_lo},
		{TSM12_REF_RST2, unused_hi},
	};
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(seq); i++) {
		ret = i2c_reg_write_byte_dt(&config->i2c, seq[i][0], seq[i][1]);
		if (ret < 0) {
			LOG_ERR("Failed to write register 0x%02x: %d", seq[i][0], ret);
			return ret;
		}
	}

	return 0;
}

static int tsm12_init(const struct device *dev)
{
	const struct tsm12_config *config = dev->config;
	struct tsm12_data *data = dev->data;
	int ret;

	if (!i2c_is_ready_dt(&config->i2c)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	data->dev = dev;
	k_work_init_delayable(&data->work, tsm12_work_handler);

	ret = tsm12_init_chip(dev);
	if (ret < 0) {
		return ret;
	}

	if (config->int_gpio.port != NULL) {
		if (!gpio_is_ready_dt(&config->int_gpio)) {
			LOG_ERR("Interrupt GPIO controller device not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&config->int_gpio, GPIO_INPUT);
		if (ret < 0) {
			LOG_ERR("Failed to configure interrupt GPIO: %d", ret);
			return ret;
		}

		gpio_init_callback(&data->int_gpio_cb, tsm12_isr, BIT(config->int_gpio.pin));
		ret = gpio_add_callback_dt(&config->int_gpio, &data->int_gpio_cb);
		if (ret < 0) {
			LOG_ERR("Failed to add GPIO callback: %d", ret);
			return ret;
		}

		ret = gpio_pin_interrupt_configure_dt(&config->int_gpio, GPIO_INT_EDGE_TO_ACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure GPIO interrupt: %d", ret);
			return ret;
		}
	}

	/* Also picks up a touch already in progress in interrupt mode */
	k_work_schedule(&data->work, K_NO_WAIT);

	return 0;
}

#define TSM12_SENSITIVITY(i, n)                                                                    \
	((DT_INST_ENUM_IDX_BY_IDX(n, threshold_windows, i) == 1 ? TSM12_SENS_HL : 0U) |            \
	 DT_INST_PROP_BY_IDX(n, threshold_midpoints, i))

#define TSM12_MIDPOINT_CHECK(i, n)                                                                 \
	BUILD_ASSERT(DT_INST_PROP_BY_IDX(n, threshold_midpoints, i) <= TSM12_MIDPOINT_MAX,         \
		     "threshold-midpoints entries must be 0-7")

#define TSM12_INIT(n)                                                                              \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, input_codes) > 0,                                         \
		     "input-codes must have at least one entry");                                  \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, input_codes) <= TSM12_NUM_CHANNELS,                       \
		     "input-codes must not have more than 12 entries");                            \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, threshold_windows) == DT_INST_PROP_LEN(n, input_codes),   \
		     "threshold-windows must have same length as input-codes");                    \
	BUILD_ASSERT(DT_INST_PROP_LEN(n, threshold_midpoints) == DT_INST_PROP_LEN(n, input_codes), \
		     "threshold-midpoints must have same length as input-codes");                  \
	LISTIFY(DT_INST_PROP_LEN(n, input_codes), TSM12_MIDPOINT_CHECK, (;), n);                   \
                                                                                                   \
	static const struct tsm12_config tsm12_config_##n = {                                      \
		.i2c = I2C_DT_SPEC_INST_GET(n),                                                    \
		.int_gpio = GPIO_DT_SPEC_INST_GET_OR(n, int_gpios, {0}),                           \
		.input_codes = DT_INST_PROP(n, input_codes),                                       \
		.sensitivity = {                                                                   \
			LISTIFY(DT_INST_PROP_LEN(n, input_codes), TSM12_SENSITIVITY, (,), n)       \
		},                                                                                 \
		.num_codes = DT_INST_PROP_LEN(n, input_codes),                                     \
	};                                                                                         \
                                                                                                   \
	static struct tsm12_data tsm12_data_##n;                                                   \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, tsm12_init, NULL, &tsm12_data_##n, &tsm12_config_##n,             \
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(TSM12_INIT)
