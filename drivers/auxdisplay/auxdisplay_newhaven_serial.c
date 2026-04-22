/*
 * Copyright (c) 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT newhaven_serial_character_lcd

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/auxdisplay.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(auxdisplay_newhaven_serial, CONFIG_AUXDISPLAY_LOG_LEVEL);

#define AUXDISPLAY_NEWHAVEN_PREFIX			0xFE

#define AUXDISPLAY_NEWHAVEN_CHARACTER_DELAY_US		100U
#define AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US		100U
#define AUXDISPLAY_NEWHAVEN_CLEAR_DELAY_US		1500U
#define AUXDISPLAY_NEWHAVEN_CURSOR_DELAY_US		1500U
#define AUXDISPLAY_NEWHAVEN_CONTRAST_DELAY_US		500U
#define AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_DELAY_US	200U

#define AUXDISPLAY_NEWHAVEN_CONTRAST_MIN		1U
#define AUXDISPLAY_NEWHAVEN_BACKLIGHT_MIN		1U
#define AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTERS		8U
#define AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_WIDTH	5U
#define AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_HEIGHT	8U
#define AUXDISPLAY_NEWHAVEN_MAX_ROWS			4U

enum auxdisplay_newhaven_command {
	AUXDISPLAY_NEWHAVEN_CMD_DISPLAY_ON = 0x41,
	AUXDISPLAY_NEWHAVEN_CMD_DISPLAY_OFF = 0x42,
	AUXDISPLAY_NEWHAVEN_CMD_SET_CURSOR = 0x45,
	AUXDISPLAY_NEWHAVEN_CMD_HOME = 0x46,
	AUXDISPLAY_NEWHAVEN_CMD_CURSOR_ON = 0x47,
	AUXDISPLAY_NEWHAVEN_CMD_CURSOR_OFF = 0x48,
	AUXDISPLAY_NEWHAVEN_CMD_BLINK_ON = 0x4B,
	AUXDISPLAY_NEWHAVEN_CMD_BLINK_OFF = 0x4C,
	AUXDISPLAY_NEWHAVEN_CMD_CLEAR = 0x51,
	AUXDISPLAY_NEWHAVEN_CMD_SET_CONTRAST = 0x52,
	AUXDISPLAY_NEWHAVEN_CMD_SET_BACKLIGHT = 0x53,
	AUXDISPLAY_NEWHAVEN_CMD_LOAD_CUSTOM_CHARACTER = 0x54,
};

union auxdisplay_newhaven_bus {
	struct i2c_dt_spec i2c;
	struct spi_dt_spec spi;
	const struct device *uart;
};

typedef bool (*auxdisplay_newhaven_bus_ready_t)(const struct device *dev);
typedef int (*auxdisplay_newhaven_bus_write_t)(const struct device *dev, const uint8_t *buf,
					       size_t len);

struct auxdisplay_newhaven_config {
	struct auxdisplay_capabilities capabilities;
	union auxdisplay_newhaven_bus bus;
	auxdisplay_newhaven_bus_ready_t bus_ready;
	auxdisplay_newhaven_bus_write_t bus_write;
	uint8_t line_addresses[AUXDISPLAY_NEWHAVEN_MAX_ROWS];
	uint8_t default_contrast;
	uint8_t default_backlight_brightness;
	uint16_t boot_delay_ms;
};

struct auxdisplay_newhaven_data {
	uint16_t cursor_x;
	uint16_t cursor_y;
	uint8_t contrast;
	uint8_t backlight;
	bool power;
	bool cursor_enabled;
	bool blink_enabled;
};

#if DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(newhaven_serial_character_lcd, i2c)
static bool auxdisplay_newhaven_bus_ready_i2c(const struct device *dev)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	return i2c_is_ready_dt(&config->bus.i2c);
}

static int auxdisplay_newhaven_bus_write_i2c(const struct device *dev, const uint8_t *buf, size_t len)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	return i2c_write_dt(&config->bus.i2c, buf, len);
}
#endif

#if DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(newhaven_serial_character_lcd, spi)
static bool auxdisplay_newhaven_bus_ready_spi(const struct device *dev)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	return spi_is_ready_dt(&config->bus.spi);
}

static int auxdisplay_newhaven_bus_write_spi(const struct device *dev, const uint8_t *buf, size_t len)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct spi_buf tx_buf = {
		.buf = (void *)buf,
		.len = len,
	};
	struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1,
	};

	return spi_write_dt(&config->bus.spi, &tx);
}
#endif

#if DT_HAS_COMPAT_ON_BUS_STATUS_OKAY(newhaven_serial_character_lcd, uart)
static bool auxdisplay_newhaven_bus_ready_uart(const struct device *dev)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	return device_is_ready(config->bus.uart);
}

static int auxdisplay_newhaven_bus_write_uart(const struct device *dev, const uint8_t *buf, size_t len)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(config->bus.uart, buf[i]);
	}

	return 0;
}
#endif

static inline int auxdisplay_newhaven_write_bus(const struct device *dev, const uint8_t *buf, size_t len)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	return config->bus_write(dev, buf, len);
}

static inline void auxdisplay_newhaven_advance_position(const struct device *dev)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct auxdisplay_newhaven_data *data = dev->data;

	data->cursor_x++;
	if (data->cursor_x >= config->capabilities.columns) {
		data->cursor_x = 0U;
		data->cursor_y++;
	}

	if (data->cursor_y >= config->capabilities.rows) {
		data->cursor_y = 0U;
	}
}

static int auxdisplay_newhaven_send_frame(const struct device *dev, const uint8_t *buf, size_t len,
					  uint32_t delay_us)
{
	int ret = auxdisplay_newhaven_write_bus(dev, buf, len);

	if (ret < 0) {
		return ret;
	}

	if (delay_us > 0U) {
		k_usleep(delay_us);
	}

	return 0;
}

static int auxdisplay_newhaven_send_command(const struct device *dev,
					    enum auxdisplay_newhaven_command command,
					    uint32_t delay_us)
{
	const uint8_t frame[] = {AUXDISPLAY_NEWHAVEN_PREFIX, command};

	return auxdisplay_newhaven_send_frame(dev, frame, sizeof(frame), delay_us);
}

static int auxdisplay_newhaven_send_command_param(const struct device *dev,
						  enum auxdisplay_newhaven_command command,
						  uint8_t param, uint32_t delay_us)
{
	const uint8_t frame[] = {AUXDISPLAY_NEWHAVEN_PREFIX, command, param};

	return auxdisplay_newhaven_send_frame(dev, frame, sizeof(frame), delay_us);
}

static int auxdisplay_newhaven_display_on(const struct device *dev)
{
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	ret = auxdisplay_newhaven_send_command(dev, AUXDISPLAY_NEWHAVEN_CMD_DISPLAY_ON,
					       AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->power = true;

	return 0;
}

static int auxdisplay_newhaven_display_off(const struct device *dev)
{
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	ret = auxdisplay_newhaven_send_command(dev, AUXDISPLAY_NEWHAVEN_CMD_DISPLAY_OFF,
					       AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->power = false;

	return 0;
}

static int auxdisplay_newhaven_cursor_set_enabled(const struct device *dev, bool enabled)
{
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	ret = auxdisplay_newhaven_send_command(dev,
					       enabled ? AUXDISPLAY_NEWHAVEN_CMD_CURSOR_ON
						       : AUXDISPLAY_NEWHAVEN_CMD_CURSOR_OFF,
					       AUXDISPLAY_NEWHAVEN_CURSOR_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->cursor_enabled = enabled;

	return 0;
}

static int auxdisplay_newhaven_position_blinking_set_enabled(const struct device *dev, bool enabled)
{
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	ret = auxdisplay_newhaven_send_command(dev,
					       enabled ? AUXDISPLAY_NEWHAVEN_CMD_BLINK_ON
						       : AUXDISPLAY_NEWHAVEN_CMD_BLINK_OFF,
					       AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->blink_enabled = enabled;

	return 0;
}

static int auxdisplay_newhaven_cursor_position_set(const struct device *dev,
						   enum auxdisplay_position type,
						   int16_t x, int16_t y)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct auxdisplay_newhaven_data *data = dev->data;
	uint16_t column;
	uint16_t row;
	int ret;

	if (type == AUXDISPLAY_POSITION_ABSOLUTE) {
		if (x < 0 || y < 0 || x >= config->capabilities.columns || y >= config->capabilities.rows) {
			return -EINVAL;
		}

		column = (uint16_t)x;
		row = (uint16_t)y;
	} else if (type == AUXDISPLAY_POSITION_RELATIVE) {
		const int new_x = ((int)data->cursor_x + x) % config->capabilities.columns;
		const int new_y = ((int)data->cursor_y + y + x / config->capabilities.columns) %
				  config->capabilities.rows;
		const int wrapped_x = new_x;
		const int wrapped_y = new_y;

		column = wrapped_x < 0 ? wrapped_x + config->capabilities.columns : wrapped_x;
		row = wrapped_y < 0 ? wrapped_y + config->capabilities.rows : wrapped_y;
	} else {
		return -ENOSYS;
	}

	if (column == 0U && row == 0U) {
		ret = auxdisplay_newhaven_send_command(dev, AUXDISPLAY_NEWHAVEN_CMD_HOME,
						       AUXDISPLAY_NEWHAVEN_CURSOR_DELAY_US);
	} else {
		ret = auxdisplay_newhaven_send_command_param(
			dev, AUXDISPLAY_NEWHAVEN_CMD_SET_CURSOR,
			config->line_addresses[row] + column,
			AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US);
	}

	if (ret < 0) {
		return ret;
	}

	data->cursor_x = column;
	data->cursor_y = row;

	return 0;
}

static int auxdisplay_newhaven_cursor_position_get(const struct device *dev, int16_t *x, int16_t *y)
{
	const struct auxdisplay_newhaven_data *data = dev->data;

	*x = (int16_t)data->cursor_x;
	*y = (int16_t)data->cursor_y;

	return 0;
}

static int auxdisplay_newhaven_capabilities_get(const struct device *dev,
						struct auxdisplay_capabilities *capabilities)
{
	const struct auxdisplay_newhaven_config *config = dev->config;

	memcpy(capabilities, &config->capabilities, sizeof(*capabilities));

	return 0;
}

static int auxdisplay_newhaven_clear(const struct device *dev)
{
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	ret = auxdisplay_newhaven_send_command(dev, AUXDISPLAY_NEWHAVEN_CMD_CLEAR,
					       AUXDISPLAY_NEWHAVEN_CLEAR_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->cursor_x = 0U;
	data->cursor_y = 0U;

	return 0;
}

static int auxdisplay_newhaven_brightness_get(const struct device *dev, uint8_t *brightness)
{
	const struct auxdisplay_newhaven_data *data = dev->data;

	*brightness = data->contrast;

	return 0;
}

static int auxdisplay_newhaven_brightness_set(const struct device *dev, uint8_t brightness)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	if (brightness < AUXDISPLAY_NEWHAVEN_CONTRAST_MIN ||
	    brightness > config->capabilities.brightness.maximum) {
		return -EINVAL;
	}

	ret = auxdisplay_newhaven_send_command_param(dev, AUXDISPLAY_NEWHAVEN_CMD_SET_CONTRAST,
						     brightness,
						     AUXDISPLAY_NEWHAVEN_CONTRAST_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->contrast = brightness;

	return 0;
}

static int auxdisplay_newhaven_backlight_get(const struct device *dev, uint8_t *backlight)
{
	const struct auxdisplay_newhaven_data *data = dev->data;

	*backlight = data->backlight;

	return 0;
}

static int auxdisplay_newhaven_backlight_set(const struct device *dev, uint8_t backlight)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	if (backlight < AUXDISPLAY_NEWHAVEN_BACKLIGHT_MIN ||
	    backlight > config->capabilities.backlight.maximum) {
		return -EINVAL;
	}

	ret = auxdisplay_newhaven_send_command_param(dev, AUXDISPLAY_NEWHAVEN_CMD_SET_BACKLIGHT,
						     backlight,
						     AUXDISPLAY_NEWHAVEN_COMMAND_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	data->backlight = backlight;

	return 0;
}

static int auxdisplay_newhaven_custom_character_set(const struct device *dev,
						    struct auxdisplay_character *character)
{
	uint8_t frame[3 + AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_HEIGHT];

	if (character->data == NULL || character->index >= AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTERS) {
		return -EINVAL;
	}

	frame[0] = AUXDISPLAY_NEWHAVEN_PREFIX;
	frame[1] = AUXDISPLAY_NEWHAVEN_CMD_LOAD_CUSTOM_CHARACTER;
	frame[2] = character->index;

	for (size_t row = 0; row < AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_HEIGHT; row++) {
		uint8_t bitmap = 0U;

		for (size_t col = 0; col < AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_WIDTH; col++) {
			if (character->data[row * AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_WIDTH + col] != 0U) {
				bitmap |= BIT(AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_WIDTH - 1U - col);
			}
		}

		frame[3 + row] = bitmap;
	}

	character->character_code = character->index;

	return auxdisplay_newhaven_send_frame(dev, frame, sizeof(frame),
					      AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_DELAY_US);
}

static int auxdisplay_newhaven_write(const struct device *dev, const uint8_t *text, uint16_t len)
{
	int ret;

	if (len == 0U) {
		return 0;
	}

	if (text == NULL) {
		return -EINVAL;
	}

	for (uint16_t i = 0; i < len; i++) {
		if (text[i] == AUXDISPLAY_NEWHAVEN_PREFIX) {
			return -EINVAL;
		}
	}

	ret = auxdisplay_newhaven_send_frame(dev, text, len,
					     (uint32_t)len * AUXDISPLAY_NEWHAVEN_CHARACTER_DELAY_US);
	if (ret < 0) {
		return ret;
	}

	for (uint16_t i = 0; i < len; i++) {
		auxdisplay_newhaven_advance_position(dev);
	}

	return 0;
}

static int auxdisplay_newhaven_init(const struct device *dev)
{
	const struct auxdisplay_newhaven_config *config = dev->config;
	struct auxdisplay_newhaven_data *data = dev->data;
	int ret;

	if (!config->bus_ready(dev)) {
		return -ENODEV;
	}

	data->cursor_x = 0U;
	data->cursor_y = 0U;
	data->contrast = config->default_contrast;
	data->backlight = config->default_backlight_brightness;
	data->power = true;
	data->cursor_enabled = false;
	data->blink_enabled = false;

	if (config->boot_delay_ms > 0U) {
		k_msleep(config->boot_delay_ms);
	}

	ret = auxdisplay_newhaven_display_on(dev);
	if (ret < 0) {
		return ret;
	}

	ret = auxdisplay_newhaven_clear(dev);
	if (ret < 0) {
		return ret;
	}

	ret = auxdisplay_newhaven_brightness_set(dev, config->default_contrast);
	if (ret < 0) {
		return ret;
	}

	ret = auxdisplay_newhaven_backlight_set(dev, config->default_backlight_brightness);
	if (ret < 0) {
		return ret;
	}

	ret = auxdisplay_newhaven_cursor_set_enabled(dev, false);
	if (ret < 0) {
		return ret;
	}

	return auxdisplay_newhaven_position_blinking_set_enabled(dev, false);
}

static DEVICE_API(auxdisplay, auxdisplay_newhaven_api) = {
	.display_on = auxdisplay_newhaven_display_on,
	.display_off = auxdisplay_newhaven_display_off,
	.cursor_set_enabled = auxdisplay_newhaven_cursor_set_enabled,
	.position_blinking_set_enabled = auxdisplay_newhaven_position_blinking_set_enabled,
	.cursor_position_set = auxdisplay_newhaven_cursor_position_set,
	.cursor_position_get = auxdisplay_newhaven_cursor_position_get,
	.capabilities_get = auxdisplay_newhaven_capabilities_get,
	.clear = auxdisplay_newhaven_clear,
	.brightness_get = auxdisplay_newhaven_brightness_get,
	.brightness_set = auxdisplay_newhaven_brightness_set,
	.backlight_get = auxdisplay_newhaven_backlight_get,
	.backlight_set = auxdisplay_newhaven_backlight_set,
	.custom_character_set = auxdisplay_newhaven_custom_character_set,
	.write = auxdisplay_newhaven_write,
};

#define AUXDISPLAY_NEWHAVEN_UART_SPEED_SUPPORTED(inst)                                          \
	COND_CODE_1(DT_INST_ON_BUS(inst, uart),                                              \
		    (((DT_PROP(DT_INST_BUS(inst), current_speed) == 300) ||                \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 1200) ||               \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 2400) ||               \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 9600) ||               \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 14400) ||              \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 19200) ||              \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 57600) ||              \
		      (DT_PROP(DT_INST_BUS(inst), current_speed) == 115200))),             \
		    (1))

#define AUXDISPLAY_NEWHAVEN_BUS_CONFIG(inst)                                                   \
	COND_CODE_1(DT_INST_ON_BUS(inst, i2c),                                               \
		    (.bus.i2c = I2C_DT_SPEC_INST_GET(inst),                                 \
		     .bus_ready = auxdisplay_newhaven_bus_ready_i2c,                        \
		     .bus_write = auxdisplay_newhaven_bus_write_i2c,),                      \
		    (COND_CODE_1(DT_INST_ON_BUS(inst, spi),                                 \
				 (.bus.spi = SPI_DT_SPEC_INST_GET(                          \
					 inst, SPI_OP_MODE_MASTER | SPI_TRANSFER_MSB |      \
						       SPI_WORD_SET(8)),            \
				  .bus_ready = auxdisplay_newhaven_bus_ready_spi,               \
				  .bus_write = auxdisplay_newhaven_bus_write_spi,),             \
				 (.bus.uart = DEVICE_DT_GET(DT_INST_BUS(inst)),               \
				  .bus_ready = auxdisplay_newhaven_bus_ready_uart,              \
				  .bus_write = auxdisplay_newhaven_bus_write_uart,))))

#define AUXDISPLAY_NEWHAVEN_DEVICE(inst)                                                       \
	BUILD_ASSERT(DT_INST_PROP(inst, rows) <= AUXDISPLAY_NEWHAVEN_MAX_ROWS,               \
		     "newhaven serial character LCD supports up to 4 rows");                 \
	BUILD_ASSERT(DT_INST_PROP_LEN(inst, line_addresses) == AUXDISPLAY_NEWHAVEN_MAX_ROWS, \
		     "line-addresses must contain 4 entries");                              \
	BUILD_ASSERT(DT_INST_PROP(inst, default_contrast) >= AUXDISPLAY_NEWHAVEN_CONTRAST_MIN && \
			     DT_INST_PROP(inst, default_contrast) <=                        \
				     DT_INST_PROP(inst, contrast_levels),                 \
		     "default-contrast must be within the supported contrast range");       \
	BUILD_ASSERT(DT_INST_PROP(inst, default_backlight_brightness) >=                     \
			     AUXDISPLAY_NEWHAVEN_BACKLIGHT_MIN &&                           \
			     DT_INST_PROP(inst, default_backlight_brightness) <=            \
				     DT_INST_PROP(inst, backlight_levels),               \
		     "default-backlight-brightness must be within the supported backlight range"); \
	BUILD_ASSERT(AUXDISPLAY_NEWHAVEN_UART_SPEED_SUPPORTED(inst),                          \
		     "unsupported UART speed for Newhaven serial character LCD");          \
	static const struct auxdisplay_newhaven_config auxdisplay_newhaven_config_##inst = { \
		.capabilities = {                                                          \
			.columns = DT_INST_PROP(inst, columns),                             \
			.rows = DT_INST_PROP(inst, rows),                                   \
			.mode = 0U,                                                         \
			.brightness.minimum = AUXDISPLAY_NEWHAVEN_CONTRAST_MIN,             \
			.brightness.maximum = DT_INST_PROP(inst, contrast_levels),          \
			.backlight.minimum = AUXDISPLAY_NEWHAVEN_BACKLIGHT_MIN,             \
			.backlight.maximum = DT_INST_PROP(inst, backlight_levels),          \
			.custom_characters = AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTERS,         \
			.custom_character_width = AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_WIDTH, \
			.custom_character_height = AUXDISPLAY_NEWHAVEN_CUSTOM_CHARACTER_HEIGHT, \
		},                                                                         \
		AUXDISPLAY_NEWHAVEN_BUS_CONFIG(inst)                                       \
		.line_addresses = DT_INST_PROP(inst, line_addresses),                      \
		.default_contrast = DT_INST_PROP(inst, default_contrast),                  \
		.default_backlight_brightness =                                           \
			DT_INST_PROP(inst, default_backlight_brightness),                  \
		.boot_delay_ms = DT_INST_PROP(inst, boot_delay_ms),                       \
	};                                                                             \
	static struct auxdisplay_newhaven_data auxdisplay_newhaven_data_##inst;        \
	DEVICE_DT_INST_DEFINE(inst, &auxdisplay_newhaven_init, NULL,                    \
			      &auxdisplay_newhaven_data_##inst,                        \
			      &auxdisplay_newhaven_config_##inst, POST_KERNEL,          \
			      CONFIG_AUXDISPLAY_INIT_PRIORITY, &auxdisplay_newhaven_api);

DT_INST_FOREACH_STATUS_OKAY(AUXDISPLAY_NEWHAVEN_DEVICE)
