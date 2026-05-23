/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT lilygo_tdeck_keyboard

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_lilygo_tdeck_keyboard, CONFIG_INPUT_LOG_LEVEL);

#define LILYGO_TDECK_KEYBOARD_COLS	5U
#define LILYGO_TDECK_KEYBOARD_ROWS	7U

#define LILYGO_TDECK_KEYBOARD_MODE_RAW_CMD	0x03U

struct lilygo_tdeck_keyboard_config {
	struct i2c_dt_spec i2c;
	uint32_t polling_interval_ms;
};

struct lilygo_tdeck_keyboard_data {
	const struct device *dev;
	struct k_work_delayable poll_work;
	uint8_t state[LILYGO_TDECK_KEYBOARD_COLS];
};

static void lilygo_tdeck_keyboard_report(const struct device *dev, uint8_t col,
					 uint8_t row, bool pressed)
{
	input_report_abs(dev, INPUT_ABS_X, col, false, K_FOREVER);
	input_report_abs(dev, INPUT_ABS_Y, row, false, K_FOREVER);
	input_report_key(dev, INPUT_BTN_TOUCH, pressed, true, K_FOREVER);
}

static void lilygo_tdeck_keyboard_poll(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lilygo_tdeck_keyboard_data *data =
		CONTAINER_OF(dwork, struct lilygo_tdeck_keyboard_data, poll_work);
	const struct device *dev = data->dev;
	const struct lilygo_tdeck_keyboard_config *cfg = dev->config;
	uint8_t new_state[LILYGO_TDECK_KEYBOARD_COLS];
	int ret;

	ret = i2c_read_dt(&cfg->i2c, new_state, sizeof(new_state));
	if (ret < 0) {
		LOG_DBG("Failed to read keyboard state: %d", ret);
		goto reschedule;
	}

	for (uint8_t col = 0U; col < LILYGO_TDECK_KEYBOARD_COLS; col++) {
		uint8_t masked = new_state[col] & BIT_MASK(LILYGO_TDECK_KEYBOARD_ROWS);
		uint8_t changed = data->state[col] ^ masked;

		if (changed == 0U) {
			continue;
		}

		for (uint8_t row = 0U; row < LILYGO_TDECK_KEYBOARD_ROWS; row++) {
			bool pressed;

			if ((changed & BIT(row)) == 0U) {
				continue;
			}

			pressed = (masked & BIT(row)) != 0U;
			lilygo_tdeck_keyboard_report(dev, col, row, pressed);
		}

		data->state[col] = masked;
	}

reschedule:
	k_work_reschedule(&data->poll_work, K_MSEC(cfg->polling_interval_ms));
}

static int lilygo_tdeck_keyboard_init(const struct device *dev)
{
	const struct lilygo_tdeck_keyboard_config *cfg = dev->config;
	struct lilygo_tdeck_keyboard_data *data = dev->data;
	uint8_t cmd = LILYGO_TDECK_KEYBOARD_MODE_RAW_CMD;
	int ret;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		LOG_ERR("I2C bus device is not ready");
		return -ENODEV;
	}

	ret = i2c_write_dt(&cfg->i2c, &cmd, sizeof(cmd));
	if (ret < 0) {
		LOG_ERR("Failed to set keyboard raw mode: %d", ret);
		return ret;
	}

	ret = i2c_read_dt(&cfg->i2c, data->state, sizeof(data->state));
	if (ret < 0) {
		memset(data->state, 0, sizeof(data->state));
		LOG_WRN("Initial keyboard state read failed: %d", ret);
	} else {
		for (uint8_t col = 0U; col < LILYGO_TDECK_KEYBOARD_COLS; col++) {
			data->state[col] &= BIT_MASK(LILYGO_TDECK_KEYBOARD_ROWS);
		}
	}

	data->dev = dev;
	k_work_init_delayable(&data->poll_work, lilygo_tdeck_keyboard_poll);
	k_work_reschedule(&data->poll_work, K_NO_WAIT);

	return 0;
}

#define LILYGO_TDECK_KEYBOARD_DEFINE(inst)						\
	BUILD_ASSERT(DT_INST_PROP(inst, polling_interval_ms) > 0,			\
		     "polling-interval-ms must be greater than 0");			\
										\
	static const struct lilygo_tdeck_keyboard_config				\
	lilygo_tdeck_keyboard_config_##inst = {					\
		.i2c = I2C_DT_SPEC_INST_GET(inst),					\
		.polling_interval_ms = DT_INST_PROP(inst, polling_interval_ms),	\
	};									\
										\
	static struct lilygo_tdeck_keyboard_data lilygo_tdeck_keyboard_data_##inst;	\
										\
	DEVICE_DT_INST_DEFINE(inst, lilygo_tdeck_keyboard_init, NULL,			\
			      &lilygo_tdeck_keyboard_data_##inst,			\
			      &lilygo_tdeck_keyboard_config_##inst, POST_KERNEL,	\
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(LILYGO_TDECK_KEYBOARD_DEFINE)
