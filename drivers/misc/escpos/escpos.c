/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT epson_escpos

#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/misc/escpos/escpos.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(escpos, CONFIG_ESCPOS_LOG_LEVEL);

/* ESC/POS command definitions */
#define ESC 0x1B
#define GS  0x1D

struct escpos_config {
	const struct device *uart_dev;
};

struct escpos_data {
	struct k_mutex lock;
};

static int escpos_write_cmd(const struct device *dev, const uint8_t *cmd, size_t len)
{
	const struct escpos_config *config = dev->config;
	struct escpos_data *data = dev->data;

	if (config->uart_dev == NULL) {
		return -ENODEV;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(config->uart_dev, cmd[i]);
	}

	k_mutex_unlock(&data->lock);

	return 0;
}

int escpos_init(const struct device *dev)
{
	uint8_t cmd[] = {ESC, '@'};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_print(const struct device *dev, const uint8_t *data, size_t len)
{
	if (data == NULL || len == 0) {
		return -EINVAL;
	}

	return escpos_write_cmd(dev, data, len);
}

int escpos_print_str(const struct device *dev, const char *str)
{
	if (str == NULL) {
		return -EINVAL;
	}

	return escpos_print(dev, (const uint8_t *)str, strlen(str));
}

int escpos_feed_lines(const struct device *dev, uint8_t lines)
{
	uint8_t cmd[] = {ESC, 'd', lines};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_cut_paper(const struct device *dev, bool partial)
{
	uint8_t cmd[] = {GS, 'V', partial ? 1 : 0};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_set_align(const struct device *dev, enum escpos_align align)
{
	if (align > ESCPOS_ALIGN_RIGHT) {
		return -EINVAL;
	}

	uint8_t cmd[] = {ESC, 'a', (uint8_t)align};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_set_font_size(const struct device *dev, enum escpos_font_size size)
{
	uint8_t cmd[] = {GS, '!', 0};

	switch (size) {
	case ESCPOS_FONT_SIZE_NORMAL:
		cmd[2] = 0x00;
		break;
	case ESCPOS_FONT_SIZE_DOUBLE_HEIGHT:
		cmd[2] = 0x01;
		break;
	case ESCPOS_FONT_SIZE_DOUBLE_WIDTH:
		cmd[2] = 0x10;
		break;
	case ESCPOS_FONT_SIZE_DOUBLE_BOTH:
		cmd[2] = 0x11;
		break;
	default:
		return -EINVAL;
	}

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_set_bold(const struct device *dev, bool enable)
{
	uint8_t cmd[] = {ESC, 'E', enable ? 1 : 0};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_set_underline(const struct device *dev, bool enable)
{
	uint8_t cmd[] = {ESC, '-', enable ? 1 : 0};

	return escpos_write_cmd(dev, cmd, sizeof(cmd));
}

int escpos_reset(const struct device *dev)
{
	return escpos_init(dev);
}

static int escpos_driver_init(const struct device *dev)
{
	struct escpos_data *data = dev->data;
	const struct escpos_config *config = dev->config;

	if (config->uart_dev == NULL) {
		LOG_ERR("UART device not specified");
		return -ENODEV;
	}

	if (!device_is_ready(config->uart_dev)) {
		LOG_ERR("UART device not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->lock);

	LOG_INF("ESC/POS printer initialized");

	return 0;
}

#define ESCPOS_DEVICE_INIT(inst)                                                                   \
	static struct escpos_data escpos_data_##inst;                                              \
                                                                                                   \
	static const struct escpos_config escpos_config_##inst = {                                 \
		.uart_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                      \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, escpos_driver_init, NULL, &escpos_data_##inst,                 \
			      &escpos_config_##inst, POST_KERNEL,                                  \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, NULL);

DT_INST_FOREACH_STATUS_OKAY(ESCPOS_DEVICE_INIT)
