/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT lilygo_t_deck_keyboard

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(input_lilygo_tdeck_keyboard, CONFIG_INPUT_LOG_LEVEL);

struct lilygo_tdeck_key_char {
	char ch;
	uint16_t code;
};

/*
 * Character mapping from the LilyGO Keyboard_ESP32C3.ino keyboard[][]
 * and keyboard_symbol[][] layouts.
 */
static const struct lilygo_tdeck_key_char lilygo_tdeck_key_chars[] = {
	{'q', INPUT_KEY_Q}, {'w', INPUT_KEY_W}, {'a', INPUT_KEY_A}, {' ', INPUT_KEY_SPACE},
	{'e', INPUT_KEY_E}, {'s', INPUT_KEY_S}, {'d', INPUT_KEY_D}, {'p', INPUT_KEY_P},
	{'x', INPUT_KEY_X}, {'z', INPUT_KEY_Z},
	{'r', INPUT_KEY_R}, {'g', INPUT_KEY_G}, {'t', INPUT_KEY_T}, {'v', INPUT_KEY_V},
	{'c', INPUT_KEY_C}, {'f', INPUT_KEY_F},
	{'u', INPUT_KEY_U}, {'h', INPUT_KEY_H}, {'y', INPUT_KEY_Y}, {'b', INPUT_KEY_B},
	{'n', INPUT_KEY_N}, {'j', INPUT_KEY_J},
	{'o', INPUT_KEY_O}, {'l', INPUT_KEY_L}, {'i', INPUT_KEY_I}, {'m', INPUT_KEY_M},
	{'k', INPUT_KEY_K},
	{'\r', INPUT_KEY_ENTER}, {'\b', INPUT_KEY_BACKSPACE}, {'\f', INPUT_KEY_TAB},
	{'1', INPUT_KEY_1}, {'2', INPUT_KEY_2}, {'3', INPUT_KEY_3}, {'4', INPUT_KEY_4},
	{'5', INPUT_KEY_5}, {'6', INPUT_KEY_6}, {'7', INPUT_KEY_7}, {'8', INPUT_KEY_8},
	{'9', INPUT_KEY_9}, {'0', INPUT_KEY_0},
	{'/', INPUT_KEY_SLASH}, {'-', INPUT_KEY_MINUS}, {'+', INPUT_KEY_EQUAL},
	{':', INPUT_KEY_SEMICOLON}, {';', INPUT_KEY_SEMICOLON}, {',', INPUT_KEY_COMMA},
	{'.', INPUT_KEY_DOT}, {'\'', INPUT_KEY_APOSTROPHE}, {'"', INPUT_KEY_APOSTROPHE},
	{'(', INPUT_KEY_LEFTBRACE}, {')', INPUT_KEY_RIGHTBRACE},
	{'*', INPUT_KEY_KPASTERISK},
};

struct lilygo_tdeck_keyboard_config {
	struct i2c_dt_spec i2c;
	uint32_t polling_interval_ms;
};

struct lilygo_tdeck_keyboard_data {
	const struct device *dev;
	struct k_work_delayable poll_work;
};

static void lilygo_tdeck_keyboard_report_key(const struct device *dev, uint16_t code)
{
	input_report_key(dev, code, 1, false, K_FOREVER);
	input_report_key(dev, code, 0, true, K_FOREVER);
}

static int lilygo_tdeck_keyboard_char_to_code(char ch, uint16_t *code)
{
	char lookup = ch;

	if (lookup >= 'A' && lookup <= 'Z') {
		lookup = lookup - 'A' + 'a';
	}

	for (size_t i = 0U; i < ARRAY_SIZE(lilygo_tdeck_key_chars); i++) {
		if (lilygo_tdeck_key_chars[i].ch == lookup) {
			*code = lilygo_tdeck_key_chars[i].code;
			return 0;
		}
	}

	return -ENOENT;
}

static void lilygo_tdeck_keyboard_report_key_char(const struct device *dev, char ch)
{
	uint16_t code;
	int ret;

	ret = lilygo_tdeck_keyboard_char_to_code(ch, &code);
	if (ret < 0) {
		LOG_DBG("Unhandled key character: 0x%02x", ch);
		return;
	}

	LOG_DBG("key character 0x%02x -> code=%u", ch, code);

	/*
	 * Key mode reports edge events as single ASCII bytes. Emit a press and
	 * release pair so downstream consumers see a complete tap.
	 */
	lilygo_tdeck_keyboard_report_key(dev, code);
}

static void lilygo_tdeck_keyboard_poll(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct lilygo_tdeck_keyboard_data *data =
		CONTAINER_OF(dwork, struct lilygo_tdeck_keyboard_data, poll_work);
	const struct device *dev = data->dev;
	const struct lilygo_tdeck_keyboard_config *cfg = dev->config;
	uint8_t key_byte;
	int ret;

	ret = i2c_read_dt(&cfg->i2c, &key_byte, sizeof(key_byte));
	if (ret < 0) {
		LOG_DBG("Failed to read keyboard: %d", ret);
		goto reschedule;
	}

	if (key_byte != 0U) {
		lilygo_tdeck_keyboard_report_key_char(dev, (char)key_byte);
	}

reschedule:
	k_work_reschedule(&data->poll_work, K_MSEC(cfg->polling_interval_ms));
}

static int lilygo_tdeck_keyboard_init(const struct device *dev)
{
	const struct lilygo_tdeck_keyboard_config *cfg = dev->config;
	struct lilygo_tdeck_keyboard_data *data = dev->data;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR_DEVICE_NOT_READY(cfg->i2c.bus);
		return -ENODEV;
	}

	/* Wait for the ESP32-C3 keyboard controller to boot. */
	k_sleep(K_MSEC(500));

	data->dev = dev;
	k_work_init_delayable(&data->poll_work, lilygo_tdeck_keyboard_poll);
	k_work_reschedule(&data->poll_work, K_NO_WAIT);

	return 0;
}

#define LILYGO_TDECK_KEYBOARD_DEFINE(inst)                                                         \
	BUILD_ASSERT(DT_INST_PROP(inst, polling_interval_ms) > 0,                                  \
		     "polling-interval-ms must be greater than 0");                                \
                                                                                                   \
	static const struct lilygo_tdeck_keyboard_config lilygo_tdeck_keyboard_config_##inst = {   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),                                                 \
		.polling_interval_ms = DT_INST_PROP(inst, polling_interval_ms),                    \
	};                                                                                         \
                                                                                                   \
	static struct lilygo_tdeck_keyboard_data lilygo_tdeck_keyboard_data_##inst;                \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, lilygo_tdeck_keyboard_init, NULL,                              \
			      &lilygo_tdeck_keyboard_data_##inst,                                  \
			      &lilygo_tdeck_keyboard_config_##inst, POST_KERNEL,                   \
			      CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(LILYGO_TDECK_KEYBOARD_DEFINE)
